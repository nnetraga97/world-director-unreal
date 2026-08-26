#!/usr/bin/env python3
"""One-shot local bridge between Unreal and a configured CLI agent.

The Unreal provider owns cancellation and timeout enforcement. This process owns
prompt construction and invokes the CLI once for exactly one generation stage.
"""

from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path
import re
import signal
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


MAX_REQUEST_BYTES = 32_000
MAX_RESPONSE_BYTES = 64_000
MAX_VALIDATION_MESSAGE_CHARS = 512
DEFAULT_COMPANION_TIMEOUT_SECONDS = 360.0

POPULATION_MIN_ITEMS = {
    "residents": 20,
    "households": 1,
    "relationships": 1,
    "beliefs": 1,
    "changeProjects": 1,
}

REPAIR_SECTION_DEFINITIONS = {
    "brief": ("object", "WorldBrief"),
    "topology": ("object", "TownTopology"),
    "locations": ("array", "WorldLocation"),
    "residents": ("array", "Resident"),
    "households": ("array", "Household"),
    "relationships": ("array", "Relationship"),
    "beliefs": ("array", "Belief"),
    "facts": ("array", "WorldFact"),
    "events": ("array", "WorldEvent"),
    "threats": ("array", "Threat"),
    "changeProjects": ("array", "ChangeProject"),
}

_ACTIVE_AGENT_PROCESS: subprocess.Popen[str] | None = None
_ACTIVE_TELEMETRY_PATH: Path | None = None


def _terminate_process_tree(process: subprocess.Popen[str], force: bool = False) -> None:
    """Terminate the CLI and its process group on POSIX hosts."""
    if process.poll() is not None:
        return
    if os.name == "posix":
        try:
            os.killpg(process.pid, signal.SIGKILL if force else signal.SIGTERM)
            return
        except ProcessLookupError:
            return
        except OSError:
            pass
    try:
        process.kill() if force else process.terminate()
    except ProcessLookupError:
        pass


def _write_interrupted_telemetry(signum: int, _frame: object) -> None:
    """Leave a bounded artifact when Unreal cancels the companion process."""
    global _ACTIVE_AGENT_PROCESS
    if _ACTIVE_AGENT_PROCESS is not None:
        _terminate_process_tree(_ACTIVE_AGENT_PROCESS)
    if _ACTIVE_TELEMETRY_PATH is not None:
        _ACTIVE_TELEMETRY_PATH.write_text(
            json.dumps(
                {
                    "companionOutcome": "interrupted",
                    "signal": signum,
                    "timedOut": False,
                    "usageAvailable": False,
                    "error": "Companion interrupted by Unreal before the CLI completed.",
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    raise SystemExit(128 + signum)


signal.signal(signal.SIGTERM, _write_interrupted_telemetry)
if hasattr(signal, "SIGINT"):
    signal.signal(signal.SIGINT, _write_interrupted_telemetry)


STAGE_KEYS = {
    "interpret": ("brief",),
    "topology": ("topology", "locations", "facts", "threats"),
    "layout": ("selectedCandidateId",),
    "population": (
        "residents",
        "households",
        "relationships",
        "beliefs",
        "events",
        "changeProjects",
    ),
    "repair": ("replacements",),
}

STAGE_DEFINITIONS = {
    "interpret": {"brief": ("object", "WorldBrief")},
    "topology": {
        "topology": ("object", "TownTopology"),
        "locations": ("array", "WorldLocation"),
        "facts": ("array", "WorldFact"),
        "threats": ("array", "Threat"),
    },
    "layout": {"selectedCandidateId": ("string", None)},
    "population": {
        "residents": ("array", "Resident"),
        "households": ("array", "Household"),
        "relationships": ("array", "Relationship"),
        "beliefs": ("array", "Belief"),
        "events": ("array", "WorldEvent"),
        "changeProjects": ("array", "ChangeProject"),
    },
    "repair": {"replacements": ("array", None)},
}


def strict_json_loads(text: str) -> dict:
    """Parse provider JSON without duplicate keys or non-standard numbers."""

    def reject_duplicate_keys(pairs: list[tuple[str, object]]) -> dict:
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ValueError(f"non-standard JSON number: {value}")

    value = json.loads(
        text,
        object_pairs_hook=reject_duplicate_keys,
        parse_constant=reject_constant,
    )
    if not isinstance(value, dict):
        raise ValueError("provider output must contain one JSON object")
    return value


def _referenced_definitions(value: object) -> set[str]:
    names: set[str] = set()
    if isinstance(value, dict):
        reference = value.get("$ref")
        if isinstance(reference, str) and reference.startswith("#/$defs/"):
            names.add(reference.rsplit("/", 1)[-1])
        for child in value.values():
            names.update(_referenced_definitions(child))
    elif isinstance(value, list):
        for child in value:
            names.update(_referenced_definitions(child))
    return names


def _repair_schema_for_target(target: dict, canonical_defs: dict) -> dict:
    section = str(target.get("section", ""))
    wire_type, definition = REPAIR_SECTION_DEFINITIONS.get(section, (None, None))
    if definition is None:
        raise ValueError(f"repair target has unsupported section '{section}'")
    value_schema: dict
    if wire_type == "array":
        item_schema = {"$ref": f"#/$defs/{definition}"}
        value_schema = {"type": "array", "items": item_schema}
    else:
        value_schema = {"$ref": f"#/$defs/{definition}"}
    properties = {
        "section": {"const": section},
        "value": value_schema,
    }
    required = ["section", "value"]
    if "index" in target and target["index"] is not None:
        properties["index"] = {"const": int(target["index"]), "type": "integer"}
        required.append("index")
        if wire_type == "array":
            properties["value"] = {"$ref": f"#/$defs/{definition}"}
    return {
        "type": "object",
        "additionalProperties": False,
        "required": required,
        "properties": properties,
    }


def stage_response_schema(request: dict) -> dict:
    """Build a neutral, stage-specific schema without fixture names or content."""
    stage = request["stage"]
    schema_path = Path(__file__).resolve().parents[1] / "Schemas" / "world-director.schema.json"
    canonical_defs = copy.deepcopy(load_json(schema_path)["$defs"])
    if stage in {"population", "repair"}:
        resident_definition = canonical_defs.get("Resident")
        if isinstance(resident_definition, dict):
            resident_properties = resident_definition.setdefault("properties", {})
            for field in ("importantMemories", "beliefIds", "relationshipIds"):
                field_schema = resident_properties.get(field)
                if isinstance(field_schema, dict):
                    field_schema["minItems"] = 1
    payload_properties = {}
    initial_defs: set[str] = set()
    for field, (wire_type, definition) in STAGE_DEFINITIONS[stage].items():
        if definition:
            initial_defs.add(definition)
            field_schema = {"$ref": f"#/$defs/{definition}"}
            if wire_type == "array":
                field_schema = {"type": "array", "items": field_schema}
                if stage == "population" and field in POPULATION_MIN_ITEMS:
                    field_schema["minItems"] = POPULATION_MIN_ITEMS[field]
        elif stage == "layout":
            candidate_ids = [
                item.get("opaqueId")
                for item in request.get("layoutCandidates", [])
                if isinstance(item, dict) and isinstance(item.get("opaqueId"), str)
            ]
            field_schema = {"type": "string", "enum": candidate_ids}
        elif stage == "repair":
            targets = [
                target
                for target in request.get("repairTargets", [])
                if isinstance(target, dict)
            ]
            if targets:
                field_schema = {
                    "type": "array",
                    "items": {
                        "oneOf": [
                            _repair_schema_for_target(target, canonical_defs)
                            for target in targets
                        ]
                    },
                }
            else:
                field_schema = {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "additionalProperties": False,
                        "required": ["section", "value"],
                        "properties": {
                            "section": {
                                "type": "string",
                                "enum": list(REPAIR_SECTION_DEFINITIONS),
                            },
                            "index": {"type": "integer", "minimum": 0},
                            "value": {},
                        },
                    },
                }
        else:
            field_schema = {"type": wire_type}
        payload_properties[field] = field_schema

    required_defs = set(initial_defs)
    required_defs.update(_referenced_definitions(payload_properties))
    pending = list(required_defs)
    while pending:
        name = pending.pop()
        definition = canonical_defs.get(name)
        if not isinstance(definition, dict):
            raise ValueError(f"canonical schema is missing definition '{name}'")
        for dependency in _referenced_definitions(definition):
            if dependency not in required_defs:
                required_defs.add(dependency)
                pending.append(dependency)

    return {
        "type": "object",
        "additionalProperties": False,
        "required": ["stage", "payload"],
        "properties": {
            "stage": {"const": stage},
            "payload": {
                "type": "object",
                "additionalProperties": False,
                "required": list(STAGE_KEYS[stage]),
                "properties": payload_properties,
            },
        },
        "$defs": {name: canonical_defs[name] for name in sorted(required_defs)},
    }


def _schema_type_matches(value: object, expected: str) -> bool:
    if expected == "object":
        return isinstance(value, dict)
    if expected == "array":
        return isinstance(value, list)
    if expected == "string":
        return isinstance(value, str)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "null":
        return value is None
    return True


def _validate_schema_value(
    value: object,
    schema: dict,
    definitions: dict,
    path: str,
) -> None:
    if "$ref" in schema:
        reference = schema["$ref"]
        if not isinstance(reference, str) or not reference.startswith("#/$defs/"):
            raise ValueError(f"{path}: unsupported schema reference")
        name = reference.rsplit("/", 1)[-1]
        definition = definitions.get(name)
        if not isinstance(definition, dict):
            raise ValueError(f"{path}: missing schema definition '{name}'")
        _validate_schema_value(value, definition, definitions, path)
        return
    if "oneOf" in schema:
        failures = []
        for option in schema["oneOf"]:
            try:
                _validate_schema_value(value, option, definitions, path)
                return
            except ValueError as exc:
                failures.append(str(exc))
        raise ValueError(f"{path}: no allowed schema variant matched")
    if "const" in schema and value != schema["const"]:
        raise ValueError(f"{path}: expected constant {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        raise ValueError(f"{path}: value is not one of the allowed choices")
    expected_type = schema.get("type")
    if isinstance(expected_type, str) and not _schema_type_matches(value, expected_type):
        article = "an " if expected_type in {"object", "array"} else ""
        raise ValueError(f"{path} must be {article}{expected_type}")
    if isinstance(value, str):
        if "minLength" in schema and len(value) < int(schema["minLength"]):
            raise ValueError(f"{path}: string is shorter than the minimum length")
        pattern = schema.get("pattern")
        if isinstance(pattern, str) and re.search(pattern, value) is None:
            raise ValueError(f"{path}: string does not match the canonical pattern")
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            raise ValueError(f"{path}: number is below the minimum")
        if "maximum" in schema and value > schema["maximum"]:
            raise ValueError(f"{path}: number is above the maximum")
    if isinstance(schema.get("not"), dict):
        try:
            _validate_schema_value(value, schema["not"], definitions, path)
        except ValueError:
            pass
        else:
            raise ValueError(f"{path}: value matches a prohibited schema")
    if isinstance(value, dict):
        required = schema.get("required", [])
        missing = [key for key in required if key not in value]
        if missing:
            raise ValueError(f"{path}: missing required field(s) {sorted(missing)}")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            extra = sorted(set(value) - set(properties))
            if extra:
                raise ValueError(f"{path}: unknown field(s) {extra}")
        for key, child in value.items():
            child_schema = properties.get(key)
            if isinstance(child_schema, dict):
                _validate_schema_value(child, child_schema, definitions, f"{path}.{key}")
    if isinstance(value, list):
        if "minItems" in schema and len(value) < int(schema["minItems"]):
            raise ValueError(f"{path}: array has too few items")
        if schema.get("uniqueItems"):
            fingerprints = {json.dumps(item, sort_keys=True, separators=(",", ":")) for item in value}
            if len(fingerprints) != len(value):
                raise ValueError(f"{path}: array items must be unique")
        item_schema = schema.get("items")
        if isinstance(item_schema, dict):
            for index, item in enumerate(value):
                _validate_schema_value(item, item_schema, definitions, f"{path}[{index}]")


def validate_json_schema(value: object, schema: dict) -> None:
    definitions = schema.get("$defs", {})
    if not isinstance(definitions, dict):
        raise ValueError("canonical schema has no definitions")
    _validate_schema_value(value, schema, definitions, "$")


def response_envelope_example(stage: str) -> dict:
    payload = {}
    for field, (wire_type, _) in STAGE_DEFINITIONS[stage].items():
        payload[field] = [] if wire_type == "array" else {} if wire_type == "object" else "string"
    return {"stage": stage, "payload": payload}


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return value


def fixture_response(request: dict) -> dict:
    fixture_path = Path(__file__).resolve().parents[1] / "Fixtures" / "living-town.json"
    fixture = load_json(fixture_path)
    stage = request["stage"]
    seed = int(request.get("seed", fixture["seed"]))
    prompt = str(request.get("playerPrompt", "")).strip()
    if stage == "interpret":
        brief = copy.deepcopy(fixture["brief"])
        brief["id"] = f"brief.generated_{seed}"
        brief["playerPrompt"] = prompt
        if prompt:
            brief["premises"][0] = prompt
        else:
            brief["premises"][0] = (
                "An original river frontier town assembled from the certified "
                "Stylized Medieval Frontier capability catalog."
            )
        return {"stage": stage, "payload": {"brief": brief}}
    if stage == "topology":
        return {
            "stage": stage,
            "payload": {key: copy.deepcopy(fixture[key]) for key in STAGE_KEYS[stage]},
        }
    if stage == "layout":
        candidates = request.get("layoutCandidates", [])
        if not candidates:
            raise ValueError("layout stage requires Unreal-authored candidates")
        selected = candidates[seed % len(candidates)]["opaqueId"]
        return {"stage": stage, "payload": {"selectedCandidateId": selected}}
    if stage == "population":
        response = {
            "stage": stage,
            "payload": {key: copy.deepcopy(fixture[key]) for key in STAGE_KEYS[stage]},
        }
        if request.get("testInvalidPopulationOnce"):
            response["payload"]["residents"][0]["homeLocationId"] = "location.missing"
        return response
    if stage == "repair":
        replacements = []
        seen = set()
        for issue in request.get("validationIssues", []):
            path = str(issue.get("path", ""))
            if path.startswith("$."):
                path = path[2:]
            match = re.match(r"^([A-Za-z]+)\[(\d+)\]", path)
            if not match:
                section = path.split(".", 1)[0]
                key = (section, None)
                if section in fixture and isinstance(fixture[section], list) and key not in seen:
                    seen.add(key)
                    replacements.append(
                        {"section": section, "value": copy.deepcopy(fixture[section])}
                    )
                continue
            section, index_text = match.groups()
            index = int(index_text)
            key = (section, index)
            if key in seen or section not in fixture or index >= len(fixture[section]):
                continue
            seen.add(key)
            replacements.append(
                {"section": section, "index": index, "value": copy.deepcopy(fixture[section][index])}
            )
        return {"stage": stage, "payload": {"replacements": replacements}}
    raise ValueError(f"unsupported stage: {stage}")


def build_agent_prompt(request: dict) -> str:
    stage = request["stage"]
    capability_summary = request.get("capabilitySummary", {})
    current = compact_current_context(request)
    candidates = request.get("layoutCandidates", [])
    validation = compact_validation_issues(request)
    world_context = request.get("worldContext", {})
    response_schema = stage_response_schema(request)
    envelope_example = response_envelope_example(stage)
    blank_rule = (
        "The player prompt is blank. Invent an original, confident world; do not ask questions."
        if not str(request.get("playerPrompt", "")).strip()
        else "Honor the player prompt only through supported capabilities."
    )
    repair_rule = ""
    if stage == "repair":
        repair_rule = (
            "Return replacements only for reported invalid sections. Use {section, index, value} "
            "for one bad array entry, or {section, value:[...]} when the error names the whole "
            "array. For object sections, replace the complete object. The supplied repairTargets "
            "are the only legal targets. Return exactly one replacement for every supplied "
            "repairTarget; never omit a target or return a partial prefix. Do not regenerate "
            "valid sections. Use only supplied fact IDs; a threat ID is not a fact or belief ID."
        )
        if request.get("replaceRepeatedFailure"):
            repair_rule += (
                " A prior targeted repair repeated a validation failure. Replace the invalid "
                "detail with the closest simpler supported alternative instead of preserving it."
            )
    population_rule = ""
    if stage in {"population", "repair"}:
        population_rule = (
            "Every resident must have at least one importantMemory, one beliefId, and one "
            "relationshipId. Every memory.factId and every belief.factId must exactly match an "
            "ID in the supplied facts array. Every resident beliefId must match an object in "
            "beliefs, and every relationshipId must match an object in relationships. Create a "
            "belief object for each referenced beliefId. The supplied facts array is closed and "
            "authoritative: never use a threat ID as a factId or belief ID."
        )
    return f"""
You are the World Director semantic generator. Complete only stage '{stage}'.
{blank_rule}
{repair_rule}
{population_rule}
Never emit Unreal asset paths, transforms, coordinates, commentary, markdown, or questions.
Return exactly one strict JSON object. Do not add keys outside this response schema and do not
emit duplicate keys, NaN, or Infinity. The following is only an outer-envelope example; populate
every nested field required by the schema:
{json.dumps(envelope_example, separators=(',', ':'), sort_keys=True)}
Stage-specific JSON Schema:
{json.dumps(response_schema, separators=(',', ':'), sort_keys=True)}
Use lower-camel-case wire names matching the supplied current document. Preserve every supplied
stable ID unless this is a targeted repair. The final town must use 20-30 residents, 12-18
locations/buildings, a connected network of reciprocal relationships, coherent households, at
least one central threat, the closed supplied fact set, and supported capability tags.
The population stage must include exactly one Proposed changeProject motivated by an existing
threat. It must use one supported conversion: Residence to temporary clinic, Workplace to
shelter, Shelter to faction headquarters, or Headquarters to active workplace. Include the
authority holder among required participants when the initiator does not own or control the
target. Use Condition.ThreatActive, Condition.Overnight, and Condition.PlayerAway, schedule it
at or after minute 1200, and require 60-1440 transition minutes.

Authoritative player prompt: {request.get('playerPrompt', '')!r}
Original root seed: {request.get('seed', 0)}
Retained world context (locked decisions and selected physical layout):
{json.dumps(world_context, separators=(',', ':'), sort_keys=True)}
Capability summary:
{json.dumps(capability_summary, separators=(',', ':'), sort_keys=True)}
Accepted stages so far:
{json.dumps(current, separators=(',', ':'), sort_keys=True)}
Opaque Unreal layout candidates:
{json.dumps(candidates, separators=(',', ':'), sort_keys=True)}
Targeted validation issues:
{json.dumps(validation, separators=(',', ':'), sort_keys=True)}
""".strip()


def compact_validation_issues(request: dict) -> list[dict]:
    """Keep every repair class visible without repeating identical issue text."""
    compact = []
    seen = set()
    for issue in request.get("validationIssues", []):
        if not isinstance(issue, dict):
            continue
        code = str(issue.get("code", ""))
        message = str(issue.get("message", ""))[:MAX_VALIDATION_MESSAGE_CHARS]
        key = (code, message)
        if request.get("stage") == "repair" and key in seen:
            continue
        seen.add(key)
        compact.append(
            {
                "code": code,
                "path": str(issue.get("path", "")),
                "message": message,
            }
        )
    return compact


def compact_current_context(request: dict) -> dict:
    """Keep later prompts focused on decisions the current stage can change."""
    current = request.get("current", {})
    if not isinstance(current, dict):
        return {}
    stage = request.get("stage")
    compact = {
        key: copy.deepcopy(current[key])
        for key in ("id", "seed")
        if key in current
    }
    if stage in {"topology", "layout", "population"} and "brief" in current:
        compact["brief"] = copy.deepcopy(current["brief"])
    if stage in {"layout", "population"}:
        for key in ("topology", "locations", "facts", "threats"):
            if key in current:
                compact[key] = copy.deepcopy(current[key])
    if stage == "repair":
        targets = [
            target
            for target in request.get("repairTargets", [])
            if isinstance(target, dict)
        ]
        for target in targets:
            section = target.get("section")
            if not isinstance(section, str) or section not in current:
                continue
            value = current[section]
            if isinstance(target.get("index"), int) and isinstance(value, list):
                index = target["index"]
                if 0 <= index < len(value):
                    if not isinstance(compact.get(section), dict):
                        compact[section] = {}
                    compact[section][str(index)] = copy.deepcopy(value[index])
            else:
                compact[section] = copy.deepcopy(value)
    return compact


def strip_code_fence(text: str) -> str:
    cleaned = text.strip()
    if cleaned.startswith("```"):
        first_newline = cleaned.find("\n")
        cleaned = cleaned[first_newline + 1 :] if first_newline >= 0 else cleaned
        if cleaned.endswith("```"):
            cleaned = cleaned[:-3]
    return cleaned.strip()


def discover_codex() -> str | None:
    """Find Codex even when Unreal was launched with Finder's minimal PATH."""
    discovered = shutil.which("codex")
    if discovered:
        return discovered

    home = Path.home()
    candidates = [
        Path("/opt/homebrew/bin/codex"),
        Path("/usr/local/bin/codex"),
        home / ".local/bin/codex",
        home / ".npm-global/bin/codex",
    ]
    if sys.platform == "darwin":
        candidates.extend(
            [
                Path("/Applications/Codex.app/Contents/Resources/codex"),
                Path("/Applications/ChatGPT.app/Contents/Resources/codex"),
                home / "Applications/Codex.app/Contents/Resources/codex",
                home / "Applications/ChatGPT.app/Contents/Resources/codex",
            ]
        )

    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def agent_environment(executable: str) -> dict[str, str]:
    """Supply package-manager runtime paths omitted by Finder-launched apps."""
    environment = os.environ.copy()
    path_entries = []
    executable_path = Path(executable).expanduser()
    if executable_path.parent != Path("."):
        path_entries.append(str(executable_path.parent))
    path_entries.extend(["/opt/homebrew/bin", "/usr/local/bin"])
    path_entries.extend(environment.get("PATH", "").split(os.pathsep))
    environment["PATH"] = os.pathsep.join(
        dict.fromkeys(entry for entry in path_entries if entry)
    )
    return environment


def artifact_path(output_path: Path, suffix: str) -> Path:
    stem = output_path.stem
    if stem.endswith("-response"):
        stem = stem[: -len("-response")]
    return output_path.with_name(f"{stem}-{suffix}")


def parse_provider_events(events_text: str) -> dict:
    telemetry = {
        "threadId": "",
        "inputTokens": 0,
        "cachedInputTokens": 0,
        "outputTokens": 0,
        "reasoningOutputTokens": 0,
        "eventCount": 0,
        "malformedEventCount": 0,
        "usageAvailable": False,
    }
    for line in events_text.splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            telemetry["malformedEventCount"] += 1
            continue
        telemetry["eventCount"] += 1
        if event.get("type") == "thread.started":
            telemetry["threadId"] = str(event.get("thread_id", ""))
        if event.get("type") != "turn.completed":
            continue
        usage = event.get("usage", {})
        if not isinstance(usage, dict):
            continue
        telemetry["usageAvailable"] = True
        for wire_name in (
            "input_tokens",
            "cached_input_tokens",
            "output_tokens",
            "reasoning_output_tokens",
        ):
            camel_name = "".join(
                [wire_name.split("_")[0]]
                + [part.title() for part in wire_name.split("_")[1:]]
            )
            try:
                telemetry[camel_name] += int(usage.get(wire_name, 0) or 0)
            except (TypeError, ValueError):
                telemetry["usageAvailable"] = False
    return telemetry


def validate_stage_response(request: dict, response: dict) -> dict:
    if not isinstance(response, dict):
        raise ValueError("agent response must be an object")
    if set(response) != {"stage", "payload"} or response.get("stage") != request["stage"]:
        raise ValueError("agent response has the wrong or non-canonical stage envelope")
    payload = response.get("payload")
    if not isinstance(payload, dict):
        raise ValueError("agent response payload must be an object")
    expected = set(STAGE_KEYS[request["stage"]])
    if set(payload) != expected:
        missing = sorted(expected - set(payload))
        extra = sorted(set(payload) - expected)
        raise ValueError(f"agent response payload keys differ; missing={missing} extra={extra}")
    schema = stage_response_schema(request)
    try:
        validate_json_schema(response, schema)
    except ValueError as exc:
        if request["stage"] == "layout" and "selectedCandidateId" in str(exc):
            raise ValueError("layout response selected an unknown candidate") from exc
        raise
    return response


def cli_response(request: dict, output_path: Path) -> dict:
    configured = os.environ.get("WORLD_DIRECTOR_CLI_COMMAND", "").strip()
    requested_model = str(request.get("model", "")).strip()
    reasoning_effort = str(request.get("reasoningEffort", "")).strip().lower()
    if requested_model and not re.fullmatch(r"[A-Za-z0-9._-]+", requested_model):
        raise ValueError("model contains unsupported characters")
    if reasoning_effort not in {"", "low", "medium", "high", "xhigh", "max", "ultra"}:
        raise ValueError("unsupported reasoning effort")
    if configured:
        command = shlex.split(configured)
        command = [
            argument.replace("{model}", requested_model).replace(
                "{reasoning}", reasoning_effort
            )
            for argument in command
        ]
    else:
        codex = discover_codex()
        if not codex:
            raise RuntimeError(
                "No configured CLI agent. Set WORLD_DIRECTOR_CLI_COMMAND or install codex "
                "in PATH, /opt/homebrew/bin, /usr/local/bin, ~/.local/bin, or a standard "
                "macOS Codex/ChatGPT application location."
            )
        command = [
            codex,
            "--ask-for-approval",
            "never",
            "--sandbox",
            "read-only",
            "exec",
            "--ignore-user-config",
            "--ephemeral",
            "--skip-git-repo-check",
            "--color",
            "never",
            "--json",
            "--output-last-message",
            "{output}",
            "-",
        ]
        if requested_model:
            command[1:1] = ["--model", requested_model]
        if reasoning_effort:
            command[1:1] = [
                "--config",
                f'model_reasoning_effort="{reasoning_effort}"',
            ]

    prompt = build_agent_prompt(request)
    if len(prompt.encode("utf-8")) > MAX_REQUEST_BYTES:
        raise ValueError(
            f"constructed prompt exceeds the {MAX_REQUEST_BYTES}-byte local CLI budget"
        )
    prompt_path = artifact_path(output_path, "prompt.txt")
    raw_response_path = artifact_path(output_path, "raw-response.txt")
    events_path = artifact_path(output_path, "provider-events.jsonl")
    telemetry_path = artifact_path(output_path, "telemetry.json")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    prompt_path.write_text(prompt + "\n", encoding="utf-8")
    provider_started = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="world-director-") as temp_dir:
        agent_output = Path(temp_dir) / "agent-response.json"
        expanded = [arg.replace("{output}", str(agent_output)) for arg in command]
        timeout_seconds = float(
            request.get("companionTimeoutSeconds", DEFAULT_COMPANION_TIMEOUT_SECONDS)
        )
        timeout_seconds = max(1.0, timeout_seconds)
        global _ACTIVE_AGENT_PROCESS, _ACTIVE_TELEMETRY_PATH
        _ACTIVE_TELEMETRY_PATH = telemetry_path
        timed_out = False
        process = subprocess.Popen(
            expanded,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=agent_environment(command[0]),
            start_new_session=(os.name == "posix"),
        )
        _ACTIVE_AGENT_PROCESS = process
        try:
            try:
                stdout, stderr = process.communicate(
                    input=prompt,
                    timeout=timeout_seconds,
                )
            except subprocess.TimeoutExpired:
                timed_out = True
                _terminate_process_tree(process)
                try:
                    stdout, stderr = process.communicate(timeout=5.0)
                except subprocess.TimeoutExpired:
                    _terminate_process_tree(process, force=True)
                    stdout, stderr = process.communicate()
        finally:
            _ACTIVE_AGENT_PROCESS = None
            _ACTIVE_TELEMETRY_PATH = None
        return_code = process.returncode
        provider_events = stdout or ""
        events_path.write_text(provider_events, encoding="utf-8")
        raw_response = ""
        if agent_output.exists():
            raw_response = agent_output.read_text(encoding="utf-8")
            raw_response_path.write_text(raw_response, encoding="utf-8")
        telemetry = parse_provider_events(provider_events)
        telemetry.update(
            {
                "runId": str(request.get("runId", "")),
                "stage": str(request.get("stage", "")),
                "attempt": int(request.get("attempt", 0) or 0),
                "requestedModel": requested_model or "CLI default (not reported)",
                "reasoningEffort": reasoning_effort or "CLI default",
                "promptCharacters": len(prompt),
                "responseCharacters": len(raw_response),
                "providerDurationSeconds": time.perf_counter() - provider_started,
                "promptPath": str(prompt_path),
                "rawResponsePath": str(raw_response_path),
                "providerEventsPath": str(events_path),
                "telemetryPath": str(telemetry_path),
                "exitCode": return_code,
                "providerStderr": (stderr or "")[-8000:],
                "timedOut": timed_out,
                "timeoutSeconds": timeout_seconds,
                "parseSuccess": False,
                "schemaSuccess": False,
                "billedCostUsd": None,
                "costNote": (
                    "Unavailable: the Codex CLI reports tokens but does not emit a "
                    "monetary charge for this authenticated run."
                ),
            }
        )
        if timed_out:
            telemetry["companionOutcome"] = "provider_timeout"
            telemetry["error"] = (
                f"CLI agent exceeded the companion timeout of {timeout_seconds:.1f} seconds."
            )
            telemetry_path.write_text(
                json.dumps(telemetry, indent=2) + "\n", encoding="utf-8"
            )
            raise RuntimeError(telemetry["error"])
        if return_code != 0:
            telemetry["companionOutcome"] = "provider_error"
            telemetry_path.write_text(
                json.dumps(telemetry, indent=2) + "\n", encoding="utf-8"
            )
            raise RuntimeError(
                f"configured CLI agent exited {return_code}: "
                f"{(stderr or '')[-2000:]}"
            )
        if not raw_response:
            telemetry["companionOutcome"] = "missing_response"
            telemetry_path.write_text(
                json.dumps(telemetry, indent=2) + "\n", encoding="utf-8"
            )
            raise RuntimeError("configured CLI agent produced no last-message file")
        if len(raw_response.encode("utf-8")) > MAX_RESPONSE_BYTES:
            telemetry["companionOutcome"] = "response_too_large"
            telemetry["responseValidationError"] = (
                f"response exceeds the {MAX_RESPONSE_BYTES}-byte local CLI budget"
            )
            telemetry_path.write_text(
                json.dumps(telemetry, indent=2) + "\n", encoding="utf-8"
            )
            raise ValueError(telemetry["responseValidationError"])
        try:
            response = strict_json_loads(strip_code_fence(raw_response))
            telemetry["parseSuccess"] = True
            validate_stage_response(request, response)
            telemetry["schemaSuccess"] = True
            telemetry["companionOutcome"] = "success"
        except Exception as exc:
            telemetry["companionOutcome"] = "response_validation_error"
            telemetry["responseValidationError"] = str(exc)
            telemetry_path.write_text(
                json.dumps(telemetry, indent=2) + "\n", encoding="utf-8"
            )
            raise
        telemetry_path.write_text(
            json.dumps(telemetry, indent=2) + "\n", encoding="utf-8"
        )

    response["diagnostics"] = telemetry
    return response


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--request", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    request = load_json(args.request)
    test_delay = float(request.get("testDelaySeconds", 0.0))
    if test_delay > 0.0:
        time.sleep(test_delay)
    stage = request.get("stage")
    if stage not in STAGE_KEYS:
        raise ValueError(f"unsupported or missing stage: {stage}")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    response = (
        fixture_response(request)
        if request.get("providerMode") == "fixture"
        else cli_response(request, args.output)
    )
    temp_output = args.output.with_suffix(args.output.suffix + ".tmp")
    temp_output.write_text(json.dumps(response, indent=2) + "\n", encoding="utf-8")
    temp_output.replace(args.output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # Companion errors are reported through process status and log.
        print(f"world_director_companion: {exc}", file=sys.stderr)
        raise SystemExit(1)
