#!/usr/bin/env python3
"""One-shot local bridge between Unreal and a configured CLI agent.

The Unreal provider owns cancellation and timeout enforcement. This process owns
prompt construction and invokes the CLI once for exactly one generation stage.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import random
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
DEFAULT_PROVIDER_ATTEMPTS = 2
MINIMUM_RETRY_SECONDS = 30.0
LANDFORMS = ("basin", "valley", "ridge", "coast", "marsh")
# Mirrors FWorldDirectorPresentation::Fields(); the first option is the default, which
# reproduces worlds generated before brief.presentation existed.
PRESENTATION_OPTIONS = {
    "fortification": ("open", "fenced", "palisade", "stockade"),
    "waterLife": ("none", "fishing", "harbor"),
    "justice": ("lenient", "stern", "grim"),
    "fire": ("hearths", "braziers", "vigil", "smouldering"),
    "timeOfDay": ("dawn", "morning", "midday", "dusk", "night"),
    "weather": ("clear", "overcast", "misty", "stormy"),
    "season": ("summer", "spring", "autumn"),
    "roofPalette": ("natural", "weathered", "sooty", "ochre", "slate", "moss"),
    "authorityColor": ("natural", "crimson", "azure", "gold", "verdant", "black"),
}
PRESENTATION_DEFAULTS = {name: options[0] for name, options in PRESENTATION_OPTIONS.items()}
# Fallback copy of the manifest Unreal sends in capabilitySummary.buildable (built from
# FWorldDirectorPresentation::Fields() and FWorldDirectorCompiler::BuildBuildingManifest()).
# Only headless callers such as Scripts/worldgen_batch.py rely on it.
DEFAULT_BUILDABLE_MANIFEST = {
    "presentation": {
        "fortification": {
            "open": "No perimeter; fields and roads run straight into town.",
            "fenced": "Low timber fence ring around the houses.",
            "palisade": "Tall timber palisade with gatehouses where roads enter.",
            "stockade": "Palisade plus watchtowers, spiked barricades and banners at the gates.",
        },
        "waterLife": {
            "none": "No boats or piers.",
            "fishing": "Two timber piers with rowboats; bridges where roads cross water. Needs coast, valley or marsh.",
            "harbor": "Four piers with moored boats, crates and barrels; bridges where roads cross water. Needs coast, valley or marsh.",
        },
        "justice": {
            "lenient": "No public punishment.",
            "stern": "A gallows beside the seat of authority.",
            "grim": "A gallows and coffins by the seat of authority, and a guillotine by the landmark.",
        },
        "fire": {
            "hearths": "Cookfires at homes only.",
            "braziers": "Lit braziers flank the landmark, the seat of authority and the gates.",
            "vigil": "Braziers along the main streets and watchfires in the square, lit day and night.",
            "smouldering": "Smoke rising from buildings the threat touches, with burnt wood piles.",
        },
        "timeOfDay": {
            "dawn": "The player arrives at 06:00.", "morning": "The player arrives at 09:00.",
            "midday": "The player arrives at 12:00.", "dusk": "The player arrives at 18:30.",
            "night": "The player arrives at 22:00.",
        },
        "weather": {
            "clear": "Clear sky.", "overcast": "Grey, flat light and a light breeze.",
            "misty": "Thick ground fog that hides the far edge of town.",
            "stormy": "Dark, desaturated light and trees and banners whipping in a gale.",
        },
        "season": {"summer": "Full green foliage.", "spring": "Fresh pale green foliage.",
                   "autumn": "Amber and rust foliage."},
        "roofPalette": {
            "natural": "The pack's own roof colours.", "weathered": "Greyed, sun-faded roofs.",
            "sooty": "Dark, soot-stained roofs.", "ochre": "Warm ochre roofs.",
            "slate": "Cool blue-grey roofs.", "moss": "Moss-green roofs.",
        },
        "authorityColor": {
            "natural": "Landmark and seat of authority share the town's roof palette.",
            "crimson": "Crimson roofs on the landmark and seat of authority.",
            "azure": "Azure roofs on the landmark and seat of authority.",
            "gold": "Gold roofs on the landmark and seat of authority.",
            "verdant": "Green roofs on the landmark and seat of authority.",
            "black": "Black roofs on the landmark and seat of authority.",
        },
    },
    "buildings": [
        {"building": "Guildhall", "when": "the landmark (first landmarkLocationIds entry)"},
        {"building": "Watermill", "when": "one Purpose.Workplace named with mill, waterwheel, weir or sluice"},
        {"building": "Windmill", "when": "one Purpose.Workplace named with windmill, granary, grain, flour or threshing"},
        {"building": "Civic hall", "when": "up to two: Purpose.Headquarters, or a Purpose.Workplace named with council, court, reeve, moot, assembly, archive or town hall"},
        {"building": "Inn", "when": "Purpose.Clinic, Purpose.Shelter, or names with inn, market, ferry, healing, pilgrim or shelter"},
        {"building": "Longhouse", "when": "names with longhouse, granary, watch or archive; other workplaces alternate inn and longhouse"},
        {"building": "Cottage", "when": "Purpose.Home; five cottage shapes in rotation"},
    ],
    "notAvailable": ("Stone castles, temples, ruins, lighthouses, large docks, snow, desert, animals, "
                     "and any tree but one village species. Keep these in prose."),
}
# The buildable manifest only helps the stages that choose what gets built.
MANIFEST_SECTIONS_BY_STAGE = {
    "interpret": ("presentation", "notAvailable"),
    "topology": ("buildings", "notAvailable"),
}

# Codex features a pure JSON generator never needs. Disabling them removes their
# tool definitions and instructions from every request's context.
CODEX_DISABLED_FEATURES = (
    "shell_tool",
    "unified_exec",
    "apps",
    "plugins",
    "memories",
    "multi_agent",
    "image_generation",
    "browser_use",
    "computer_use",
    "in_app_browser",
    "goals",
    "hooks",
    "view_image",
    "tool_suggest",
    "skill_search",
    "skill_mcp_dependency_install",
    "plugin_discovery_prefetch",
)

# Keywords that never belong in an output schema sent to the provider's strict
# structured-output mode. The full canonical schema is still enforced locally.
PROVIDER_SCHEMA_DROPPED_KEYWORDS = {"not", "uniqueItems", "minLength", "maxLength", "$schema", "$id", "title"}

POPULATION_MIN_ITEMS = {
    "residents": 20,
    "households": 1,
    "relationships": 1,
    "beliefs": 1,
    "changeProjects": 1,
}

STAGE_ARRAY_BOUNDS = {
    ("topology", "locations"): (12, 18),
    ("topology", "facts"): (1, None),
    ("topology", "threats"): (1, None),
    ("population", "residents"): (20, 30),
    ("population", "households"): (1, 6),
    ("population", "relationships"): (1, None),
    ("population", "beliefs"): (1, None),
    ("population", "changeProjects"): (1, 1),
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
    """Terminate the CLI and its process tree on supported desktop hosts."""
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
    elif os.name == "nt":
        try:
            command = ["taskkill", "/PID", str(process.pid), "/T"]
            if force:
                command.append("/F")
            subprocess.run(command, capture_output=True, check=False, timeout=5.0)
            return
        except (OSError, subprocess.SubprocessError):
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


def provider_output_schema(schema: object) -> object:
    """Project a canonical stage schema onto the provider's strict structured-output subset.

    Strict mode needs every object closed with all properties required and does not
    accept some validation keywords. Dropping a keyword here only relaxes what the
    provider enforces while decoding; the canonical schema is still validated locally.
    """
    if isinstance(schema, list):
        return [provider_output_schema(item) for item in schema]
    if not isinstance(schema, dict):
        return schema
    projected: dict = {}
    for key, value in schema.items():
        if key in PROVIDER_SCHEMA_DROPPED_KEYWORDS:
            continue
        if key == "oneOf":
            projected["anyOf"] = provider_output_schema(value)
            continue
        projected[key] = provider_output_schema(value)
    if projected.get("type") == "object" and isinstance(projected.get("properties"), dict):
        projected["additionalProperties"] = False
        projected["required"] = list(projected["properties"])
    return projected


def codex_home() -> Path:
    configured = os.environ.get("CODEX_HOME", "").strip()
    return Path(configured).expanduser() if configured else Path.home() / ".codex"


def load_codex_model_catalog() -> dict[str, list[str]] | None:
    """Return {model slug: supported reasoning efforts} from Codex's own model cache."""
    try:
        data = json.loads((codex_home() / "models_cache.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    catalog: dict[str, list[str]] = {}
    for model in data.get("models", []) if isinstance(data, dict) else []:
        if not isinstance(model, dict) or not isinstance(model.get("slug"), str):
            continue
        catalog[model["slug"]] = [
            str(level.get("effort"))
            for level in model.get("supported_reasoning_levels", [])
            if isinstance(level, dict) and level.get("effort")
        ]
    return catalog or None


def validate_model_choice(model: str, reasoning_effort: str) -> None:
    """Fail before spending a provider call on a model Codex no longer serves."""
    catalog = load_codex_model_catalog()
    if catalog is None or not model:
        return
    if model not in catalog:
        raise ValueError(
            f"model '{model}' is not in the installed Codex model catalog; "
            f"choose one of: {', '.join(sorted(catalog))}"
        )
    supported = catalog[model]
    if reasoning_effort and supported and reasoning_effort not in supported:
        raise ValueError(
            f"model '{model}' does not support reasoning effort '{reasoning_effort}'; "
            f"supported: {', '.join(supported)}"
        )


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
        bounds = STAGE_ARRAY_BOUNDS.get(("population", section))
        if bounds:
            minimum, maximum = bounds
            if minimum is not None:
                value_schema["minItems"] = minimum
            if maximum is not None:
                value_schema["maxItems"] = maximum
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
                bounds = STAGE_ARRAY_BOUNDS.get((stage, field))
                if bounds:
                    minimum, maximum = bounds
                    if minimum is not None:
                        field_schema["minItems"] = minimum
                    if maximum is not None:
                        field_schema["maxItems"] = maximum
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
        if "maxItems" in schema and len(value) > int(schema["maxItems"]):
            raise ValueError(f"{path}: array has too many items")
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
        if request.get("populationBundleRepair"):
            return {
                "stage": stage,
                "payload": {
                    "replacements": [
                        {"section": section, "value": copy.deepcopy(fixture[section])}
                        for section in STAGE_KEYS["population"]
                    ]
                },
            }
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


def build_agent_prompt(request: dict, include_schema: bool = True) -> str:
    """Build the stage prompt.

    include_schema=False is used when the schema travels as the provider's structured
    output format instead; repeating it in the prompt only costs input tokens.
    """
    stage = request["stage"]
    capability_summary = stage_capability_summary(request)
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
        if stage == "population":
            population_rule += (
                " Every nonempty ownerResidentId and controllerResidentId in the supplied "
                "locations must exactly match a generated resident ID. Household membership, "
                "resident householdId/homeLocationId, and home residentCapacity must agree. "
                "Prefer a compact connected relationship graph; do not emit redundant edges."
            )
    topology_rule = ""
    interpretation_rule = ""
    if stage == "interpret":
        interpretation_rule = (
            "Commit to one strong landscape identity and record it in brief.landform as exactly "
            "one of: basin (sheltered bowl or caldera), valley (river valley or pass), ridge "
            "(highland spine or hills), coast (shoreline or harbor), marsh (wetland or reed "
            "channels). Unreal builds the terrain from brief.landform alone. Make "
            "terrainPreferences describe only that landform concretely enough to drive water, "
            "settlement morphology, sightlines, and cultivated ground; never name another "
            "landform, even to rule it out. Give the place a memorable proper identity and make "
            "its economy and central pressure visibly legible in the environment. "
            "Only brief.landform and brief.presentation change what is built; prose fields are "
            "shown as text. Pick each brief.presentation value from "
            "capabilitySummary.buildable.presentation so the premise is visible: defences that "
            "match the danger, water life for a waterside economy, justice that matches the "
            "ruler, fire for vigils or a recent burning, and timeOfDay, weather, season and "
            "colours for the mood. Anything in buildable.notAvailable cannot be built; carry "
            "it through these choices or leave it to prose."
        )
    if stage == "topology":
        topology_rule = (
            "Generate 12-18 unique locations with one to six homes whose combined residentCapacity "
            "supports 30 residents. Every edge, landmark, and threat location reference must use "
            "one of those location IDs. Include at least one repurposable Home, Workplace, Shelter, "
            "or Headquarters for the later change project. Use two to four districts with distinct "
            "physical identities. Compose a readable town: one landmark terminates a deliberate "
            "civic approach; workplaces and public anchors form a compact center; homes gather in "
            "recognizable wards; one cultivated or transport edge connects the town to its terrain. "
            "Keep the edge graph connected and hierarchical, with a civic spine and local branches "
            "instead of a dense mesh. Location names and purposes must make these visual roles clear. "
            "Give exactly one location purposeTag Purpose.Landmark and list it first in "
            "landmarkLocationIds; include at least one Purpose.Workplace and at least one location "
            "with accessPolicy Public. Name owners and controllers with person-like IDs such as "
            "resident.firstname.surname, and name the governing person in governance using that "
            "same name. Location names choose buildings: use the words listed in "
            "capabilitySummary.buildable.buildings to get a matching building; a name with none "
            "of them gets a generic inn or longhouse."
        )
    schema_block = (
        "Stage-specific JSON Schema:\n"
        + json.dumps(response_schema, separators=(',', ':'), sort_keys=True)
        if include_schema
        else "The response format is enforced by the attached output schema."
    )
    town_contract = ""
    if stage in {"population", "repair"}:
        town_contract = (
            "The final town must use 20-30 residents, 12-18 locations/buildings, a connected "
            "network of reciprocal relationships, coherent households, at least one central "
            "threat, the closed supplied fact set, and supported capability tags. The population "
            "stage must include exactly one Proposed changeProject motivated by an existing "
            "threat. It must use one supported conversion: Residence to temporary clinic, "
            "Workplace to shelter, Shelter to faction headquarters, or Headquarters to active "
            "workplace. Include the authority holder among required participants when the "
            "initiator does not own or control the target. Use Condition.ThreatActive, "
            "Condition.Overnight, and Condition.PlayerAway, schedule it at or after minute 1200, "
            "and require 60-1440 transition minutes."
        )
    return f"""
You are the World Director semantic generator. Complete only stage '{stage}'.
{blank_rule}
{repair_rule}
{population_rule}
{interpretation_rule}
{topology_rule}
Never emit Unreal asset paths, transforms, coordinates, commentary, markdown, or questions.
Return exactly one strict JSON object. Do not add keys outside this response schema and do not
emit duplicate keys, NaN, or Infinity. The following is only an outer-envelope example; populate
every nested field required by the schema:
{json.dumps(envelope_example, separators=(',', ':'), sort_keys=True)}
{schema_block}
Use lower-camel-case wire names matching the supplied current document. Preserve every supplied
stable ID unless this is a targeted repair.
{town_contract}

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
        if request.get("populationBundleRepair"):
            for key in ("brief", "topology", "locations", "facts", "threats"):
                if key in current:
                    compact[key] = copy.deepcopy(current[key])
            return compact
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


def _supported_tags(request: dict) -> set[str]:
    summary = request.get("capabilitySummary", {})
    if not isinstance(summary, dict):
        return set()
    tags: set[str] = set()
    for key, value in summary.items():
        if key == "supportedTags" or key.startswith("supported"):
            if isinstance(value, list):
                tags.update(item for item in value if isinstance(item, str))
    return tags


def _unique_strings(values: object, allowed: set[str] | None = None) -> list[str]:
    result: list[str] = []
    for value in values if isinstance(values, list) else []:
        if not isinstance(value, str) or not value or value in result:
            continue
        if allowed is not None and value not in allowed:
            continue
        result.append(value)
    return result


def _clamp(value: object, minimum: float = 0.0, maximum: float = 1.0) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError):
        number = minimum
    if number != number or number in {float("inf"), float("-inf")}:
        number = minimum
    return max(minimum, min(maximum, number))


def _stable_id(value: object, fallback: str) -> str:
    text = str(value or "").strip()
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", text):
        return text
    cleaned = re.sub(r"[^A-Za-z0-9._-]+", "_", text).strip("._-")
    if cleaned and cleaned[0].isalnum():
        return cleaned
    return fallback


def _replace_exact_string(value: object, old: str, new: str) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if child == old:
                value[key] = new
            else:
                _replace_exact_string(child, old, new)
    elif isinstance(value, list):
        for index, child in enumerate(value):
            if child == old:
                value[index] = new
            else:
                _replace_exact_string(child, old, new)


def normalize_topology_response(request: dict, response: dict) -> list[str]:
    """Apply only deterministic capability invariants before later stages consume topology."""
    if request.get("stage") != "topology":
        return []
    payload = response["payload"]
    locations = payload["locations"]
    changes: list[str] = []
    topology = payload["topology"]
    districts = _unique_strings(topology.get("districts", []))
    if len(districts) > 4:
        districts = districts[:4]
        changes.append("focused the settlement into four physically distinct districts")
    while len(districts) < 2:
        districts.append(f"district.{len(districts) + 1}")
        changes.append("added a district anchor for readable settlement composition")
    topology["districts"] = districts
    for index, location in enumerate(locations):
        for field in ("ownerResidentId", "controllerResidentId"):
            value = location.get(field, "")
            if value:
                stable = _stable_id(value, f"resident.authority.{index + 1}")
                if stable != value:
                    location[field] = stable
                    changes.append(f"normalized an unstable {field} on {location.get('id', index)}")
        if not location.get("ownerResidentId") and not location.get("controllerResidentId"):
            location["controllerResidentId"] = f"resident.authority.{index + 1}"
            changes.append(f"assigned deterministic authority to {location.get('id', index)}")

    # A topology can name two authorities per location, while the population contract is
    # intentionally capped at 30 residents. Collapse overflow identities here, before the
    # creative topology is accepted, so local population synthesis always has a solution.
    retained_authorities: list[str] = []
    overflow_authorities: dict[str, str] = {}
    for location in locations:
        for field in ("ownerResidentId", "controllerResidentId"):
            authority = location.get(field, "")
            if not authority or authority in retained_authorities:
                continue
            if authority in overflow_authorities:
                location[field] = overflow_authorities[authority]
                continue
            if len(retained_authorities) < 30:
                retained_authorities.append(authority)
                continue
            replacement = retained_authorities[len(overflow_authorities) % len(retained_authorities)]
            overflow_authorities[authority] = replacement
            location[field] = replacement
    if overflow_authorities:
        changes.append(
            f"consolidated {len(overflow_authorities)} overflow authority identities "
            "into the 30-resident population limit"
        )
    homes = [item for item in locations if item.get("purposeTag") == "Purpose.Home"]
    if homes:
        capacity = sum(max(0, int(item.get("residentCapacity", 0))) for item in homes)
        missing = max(0, 30 - capacity)
        for offset in range(missing):
            home = homes[offset % len(homes)]
            home["residentCapacity"] = max(0, int(home.get("residentCapacity", 0))) + 1
        if missing:
            changes.append(f"expanded certified home capacity by {missing} to support 30 residents")
    conversions = {"Purpose.Home", "Purpose.Workplace", "Purpose.Shelter", "Purpose.Headquarters"}
    if not any(item.get("bRepurposable") and item.get("purposeTag") in conversions for item in locations):
        candidate = next((item for item in locations if item.get("purposeTag") in conversions), None)
        if candidate is not None:
            candidate["bRepurposable"] = True
            changes.append(f"made {candidate.get('id', 'one location')} eligible for the required change project")

    # Unreal builds the landmark shell from landmarkLocationIds[0] and requires a
    # Purpose.Landmark location. Keep only real IDs and put a true landmark first.
    location_by_id = {item.get("id"): item for item in locations}
    landmark_ids = [
        item for item in _unique_strings(topology.get("landmarkLocationIds", []))
        if item in location_by_id
    ]
    purpose_landmarks = [
        item["id"] for item in locations if item.get("purposeTag") == "Purpose.Landmark"
    ]
    first_true_landmark = next(
        (item for item in landmark_ids if item in purpose_landmarks),
        purpose_landmarks[0] if purpose_landmarks else None,
    )
    if first_true_landmark is not None:
        ordered = [first_true_landmark] + [item for item in landmark_ids if item != first_true_landmark]
        if ordered != topology.get("landmarkLocationIds"):
            topology["landmarkLocationIds"] = ordered
            changes.append(f"listed Purpose.Landmark location {first_true_landmark} first among landmarks")
        if not any(item.get("accessPolicy") == "Public" for item in locations):
            location_by_id[first_true_landmark]["accessPolicy"] = "Public"
            changes.append(f"opened landmark {first_true_landmark} to the public")
    return changes


def validate_topology_semantics(request: dict, response: dict) -> None:
    if request.get("stage") != "topology":
        return
    payload = response["payload"]
    locations = payload["locations"]
    location_ids = [item.get("id") for item in locations]
    if len(set(location_ids)) != len(location_ids):
        raise ValueError("topology location IDs must be unique")
    home_count = sum(item.get("purposeTag") == "Purpose.Home" for item in locations)
    if not 1 <= home_count <= 6:
        raise ValueError("topology requires one to six home locations")
    if sum(int(item.get("residentCapacity", 0)) for item in locations if item.get("purposeTag") == "Purpose.Home") < 30:
        raise ValueError("topology home capacity cannot support the permitted population")
    valid_locations = set(location_ids)
    topology = payload["topology"]
    district_count = len(topology.get("districts", []))
    if not 2 <= district_count <= 4:
        raise ValueError("topology requires two to four focused districts")
    for edge in topology.get("edges", []):
        if edge.get("fromLocationId") not in valid_locations or edge.get("toLocationId") not in valid_locations:
            raise ValueError("topology edge references an unknown location")
    if any(item not in valid_locations for item in topology.get("landmarkLocationIds", [])):
        raise ValueError("topology landmark references an unknown location")
    for threat in payload["threats"]:
        if any(item not in valid_locations for item in threat.get("affectedLocationIds", [])):
            raise ValueError("topology threat references an unknown location")
    if not any(item.get("bRepurposable") for item in locations):
        raise ValueError("topology has no location eligible for the required change project")
    # These mirror Unreal's full-slice rules (ValidateFullSliceWorldSpec). Catching them
    # here lets the stage retry with the exact error instead of failing integration.
    purposes = [item.get("purposeTag") for item in locations]
    if "Purpose.Landmark" not in purposes:
        raise ValueError("topology requires one location with purposeTag Purpose.Landmark")
    if "Purpose.Workplace" not in purposes:
        raise ValueError("topology requires at least one Purpose.Workplace location")
    if not any(item.get("accessPolicy") == "Public" for item in locations):
        raise ValueError("topology requires at least one location with accessPolicy Public")
    landmark_ids = topology.get("landmarkLocationIds", [])
    if not landmark_ids or next(
        (item.get("purposeTag") for item in locations if item.get("id") == landmark_ids[0]), None
    ) != "Purpose.Landmark":
        raise ValueError("topology landmarkLocationIds must start with the Purpose.Landmark location")


def validate_brief_semantics(request: dict, response: dict) -> None:
    if request.get("stage") != "interpret":
        return
    landform = response["payload"]["brief"].get("landform")
    if landform not in LANDFORMS:
        raise ValueError(f"brief.landform must be one of {', '.join(LANDFORMS)}")
    presentation = response["payload"]["brief"].get("presentation")
    if not isinstance(presentation, dict):
        raise ValueError("brief.presentation must be an object")
    for name, options in PRESENTATION_OPTIONS.items():
        if presentation.get(name) not in options:
            raise ValueError(f"brief.presentation.{name} must be one of {', '.join(options)}")


def normalize_brief_response(request: dict, response: dict) -> list[str]:
    """Give briefs written before brief.presentation existed the defaults Unreal also uses."""
    if request.get("stage") != "interpret":
        return []
    brief = response["payload"].get("brief")
    if not isinstance(brief, dict) or "presentation" in brief:
        return []
    brief["presentation"] = dict(PRESENTATION_DEFAULTS)
    return ["brief.presentation was missing; applied the defaults"]


def stage_capability_summary(request: dict) -> dict:
    """Keep only the manifest sections the current stage acts on."""
    summary = dict(request.get("capabilitySummary", {}))
    buildable = summary.pop("buildable", None) or DEFAULT_BUILDABLE_MANIFEST
    sections = MANIFEST_SECTIONS_BY_STAGE.get(request.get("stage"))
    if isinstance(buildable, dict) and sections:
        summary["buildable"] = {key: buildable[key] for key in sections if key in buildable}
    return summary


def normalize_population_response(request: dict, response: dict) -> list[str]:
    """Repair cross-object invariants without asking a model to rewrite an entire social graph."""
    if request.get("stage") != "population":
        return []
    payload = response["payload"]
    current = request.get("current", {})
    locations = current.get("locations", []) if isinstance(current, dict) else []
    facts = current.get("facts", []) if isinstance(current, dict) else []
    residents = payload["residents"]
    changes: list[str] = []
    location_by_id = {
        item.get("id"): item for item in locations
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    fact_by_id = {
        item.get("id"): item for item in facts
        if isinstance(item, dict) and isinstance(item.get("id"), str)
    }
    if not location_by_id or not fact_by_id:
        raise ValueError("population normalization requires accepted locations and facts")
    resident_ids = [item.get("id") for item in residents]
    if len(set(resident_ids)) != len(resident_ids):
        raise ValueError("population resident IDs must be unique")

    # Topology names authority before the population exists. Preserve those stable IDs by
    # renaming otherwise-unreserved residents and updating the entire population bundle.
    authority_ids = _unique_strings([
        authority
        for location in locations
        for authority in (location.get("ownerResidentId"), location.get("controllerResidentId"))
        if isinstance(authority, str) and authority
    ])
    for authority_id in authority_ids:
        if authority_id in resident_ids:
            continue
        candidate = next((item for item in residents if item.get("id") not in authority_ids), None)
        if candidate is None:
            raise ValueError(f"population cannot preserve topology authority ID {authority_id!r}")
        old_id = candidate["id"]
        candidate["id"] = authority_id
        _replace_exact_string(payload, old_id, authority_id)
        resident_ids[resident_ids.index(old_id)] = authority_id
        changes.append(f"preserved topology authority {authority_id} by remapping {old_id}")

    valid_residents = set(resident_ids)
    valid_locations = set(location_by_id)
    valid_facts = set(fact_by_id)
    first_fact = next(iter(fact_by_id))
    supported = _supported_tags(request)

    homes = [item for item in locations if item.get("purposeTag") == "Purpose.Home"]
    if not homes:
        raise ValueError("population cannot be assigned because topology has no home")
    remaining = {item["id"]: max(0, int(item.get("residentCapacity", 0))) for item in homes}
    assigned_home: dict[str, str] = {}
    for resident in residents:
        preferred = resident.get("homeLocationId")
        home_id = preferred if preferred in remaining and remaining[preferred] > 0 else next(
            (item["id"] for item in homes if remaining[item["id"]] > 0), ""
        )
        if not home_id:
            raise ValueError("topology home capacity is smaller than the generated population")
        remaining[home_id] -= 1
        assigned_home[resident["id"]] = home_id
        resident["homeLocationId"] = home_id
        if resident.get("currentLocationId") not in valid_locations:
            resident["currentLocationId"] = home_id
            changes.append(f"moved {resident['id']} from an unknown current location to home")
        if resident.get("bEmployed"):
            if resident.get("workplaceLocationId") not in valid_locations:
                workplace = next(
                    (item["id"] for item in locations if item.get("purposeTag") == "Purpose.Workplace"),
                    home_id,
                )
                resident["workplaceLocationId"] = workplace
                changes.append(f"assigned {resident['id']} to a valid workplace")
            if supported and resident.get("occupationTag") not in supported:
                occupation = next((tag for tag in supported if tag.startswith("Occupation.")), "")
                if occupation:
                    resident["occupationTag"] = occupation
                    changes.append(f"mapped {resident['id']} to a supported occupation")
        else:
            resident["workplaceLocationId"] = ""
        memories = resident.get("importantMemories", [])
        if not memories:
            memories.append({
                "id": f"memory.generated.{resident['id']}",
                "factId": first_fact,
                "summary": "Remembers how the settlement's shared danger changed daily life.",
                "day": 0,
                "emotionalSignificance": 0.65,
            })
            changes.append(f"seeded a grounded memory for {resident['id']}")
        for memory in memories:
            if memory.get("factId") not in valid_facts:
                memory["factId"] = first_fact
                changes.append(f"grounded an invalid memory fact for {resident['id']}")
            memory["day"] = max(0, int(memory.get("day", 0)))
            memory["emotionalSignificance"] = _clamp(memory.get("emotionalSignificance", 0.5))

    # Rebuild household membership from the already-authored resident/home choices. This is
    # lossless for character identity and avoids asking a model to synchronize three indexes.
    household_by_home = {
        item.get("homeLocationId"): item for item in payload["households"]
        if isinstance(item, dict) and item.get("homeLocationId") in remaining
    }
    rebuilt_households = []
    used_household_ids: set[str] = set()
    for home in homes:
        members = [resident_id for resident_id, home_id in assigned_home.items() if home_id == home["id"]]
        if not members:
            continue
        household = copy.deepcopy(household_by_home.get(home["id"], {}))
        candidate_id = household.get("id")
        if not isinstance(candidate_id, str) or not candidate_id or candidate_id in used_household_ids:
            candidate_id = f"household.generated.{len(rebuilt_households) + 1}"
        used_household_ids.add(candidate_id)
        household.update({
            "version": 1,
            "id": candidate_id,
            "homeLocationId": home["id"],
            "memberResidentIds": members,
        })
        rebuilt_households.append(household)
        for resident in residents:
            if resident["id"] in members:
                resident["householdId"] = candidate_id
    if rebuilt_households != payload["households"]:
        payload["households"] = rebuilt_households
        changes.append("reconciled household membership, homes, and capacities")

    # Normalize beliefs to the closed fact and resident sets, then guarantee every resident has
    # one held belief. Invalid provider references become visible telemetry rather than repairs.
    beliefs = []
    belief_ids: set[str] = set()
    for belief in payload["beliefs"]:
        belief_id = belief.get("id")
        if not isinstance(belief_id, str) or not belief_id or belief_id in belief_ids:
            continue
        holder = belief.get("holderResidentId")
        if holder not in valid_residents:
            continue
        belief["factId"] = belief.get("factId") if belief.get("factId") in valid_facts else first_fact
        if belief.get("sourceResidentId") not in valid_residents:
            belief["sourceResidentId"] = ""
        for field in ("confidence", "secrecy", "emotionalSignificance", "willingnessToShare"):
            belief[field] = _clamp(belief.get(field, 0.5))
        belief_ids.add(belief_id)
        beliefs.append(belief)
    for resident in residents:
        held = [item["id"] for item in beliefs if item["holderResidentId"] == resident["id"]]
        if not held:
            belief_id = f"belief.generated.{resident['id']}"
            belief = {
                "version": 1,
                "id": belief_id,
                "holderResidentId": resident["id"],
                "factId": first_fact,
                "confidence": 0.72,
                "bImportantSecret": False,
                "sourceResidentId": "",
                "secrecy": 0.2,
                "emotionalSignificance": 0.6,
                "willingnessToShare": 0.65,
            }
            beliefs.append(belief)
            belief_ids.add(belief_id)
            held = [belief_id]
            changes.append(f"seeded a fact-grounded belief for {resident['id']}")
        resident["beliefIds"] = held
    secret_holder_index = 0
    for fact in fact_by_id.values():
        if fact.get("bSecret") and not any(item["factId"] == fact["id"] for item in beliefs):
            holder = residents[secret_holder_index % len(residents)]
            belief_id = f"belief.generated.secret.{len(beliefs) + 1}"
            while belief_id in belief_ids:
                belief_id += ".next"
            beliefs.append({
                "version": 1,
                "id": belief_id,
                "holderResidentId": holder["id"],
                "factId": fact["id"],
                "confidence": 0.72,
                "bImportantSecret": bool(fact.get("bEstablished")),
                "sourceResidentId": "",
                "secrecy": 0.78,
                "emotionalSignificance": 0.65,
                "willingnessToShare": 0.22,
            })
            belief_ids.add(belief_id)
            holder["beliefIds"].append(belief_id)
            secret_holder_index += 1
            changes.append(f"assigned secret fact {fact['id']} to a distinct resident belief")
    payload["beliefs"] = beliefs

    # Keep valid authored relationships, downgrade broken reciprocal claims, and add a small
    # deterministic ring. The ring guarantees a connected social graph without replacing prose.
    relationships = []
    relationship_ids: set[str] = set()
    for relationship in payload["relationships"]:
        relationship_id = relationship.get("id")
        source = relationship.get("sourceResidentId")
        target = relationship.get("targetResidentId")
        if (not isinstance(relationship_id, str) or not relationship_id or
                relationship_id in relationship_ids or source not in valid_residents or
                target not in valid_residents or source == target):
            continue
        for field in ("trust", "affinity", "fear", "obligation"):
            relationship[field] = _clamp(relationship.get(field, 0.5))
        relationship_ids.add(relationship_id)
        relationships.append(relationship)
    by_id = {item["id"]: item for item in relationships}
    for relationship in relationships:
        reciprocal = by_id.get(relationship.get("reciprocalRelationshipId"))
        if relationship.get("bBidirectional") and not (
            reciprocal
            and reciprocal.get("sourceResidentId") == relationship.get("targetResidentId")
            and reciprocal.get("targetResidentId") == relationship.get("sourceResidentId")
            and reciprocal.get("relationshipType") == relationship.get("relationshipType")
            and reciprocal.get("reciprocalRelationshipId") == relationship.get("id")
        ):
            relationship["bBidirectional"] = False
            relationship["reciprocalRelationshipId"] = ""
            changes.append(f"removed an invalid reciprocal claim from {relationship['id']}")
    adjacency = {resident_id: set() for resident_id in resident_ids}
    for relationship in relationships:
        adjacency[relationship["sourceResidentId"]].add(relationship["targetResidentId"])
        adjacency[relationship["targetResidentId"]].add(relationship["sourceResidentId"])
    components: list[list[str]] = []
    unseen = set(resident_ids)
    while unseen:
        start = next(item for item in resident_ids if item in unseen)
        component: list[str] = []
        pending = [start]
        while pending:
            resident_id = pending.pop()
            if resident_id not in unseen:
                continue
            unseen.remove(resident_id)
            component.append(resident_id)
            pending.extend(adjacency[resident_id] & unseen)
        components.append(component)
    for index in range(len(components) - 1):
        source = components[index][0]
        target = components[index + 1][0]
        forward_id = f"relationship.generated.bridge.{index}.forward"
        reverse_id = f"relationship.generated.bridge.{index}.reverse"
        suffix = 1
        while forward_id in relationship_ids or reverse_id in relationship_ids:
            forward_id = f"relationship.generated.bridge.{index}.{suffix}.forward"
            reverse_id = f"relationship.generated.bridge.{index}.{suffix}.reverse"
            suffix += 1
        base = {
            "version": 1,
            "relationshipType": "Relationship.CommunityTie",
            "bBidirectional": True,
            "trust": 0.58,
            "affinity": 0.5,
            "fear": 0.08,
            "obligation": 0.45,
        }
        relationships.append({
            **base, "id": forward_id, "sourceResidentId": source,
            "targetResidentId": target, "reciprocalRelationshipId": reverse_id,
        })
        relationships.append({
            **base, "id": reverse_id, "sourceResidentId": target,
            "targetResidentId": source, "reciprocalRelationshipId": forward_id,
        })
        relationship_ids.update({forward_id, reverse_id})
    payload["relationships"] = relationships
    for resident in residents:
        resident["relationshipIds"] = [
            item["id"] for item in relationships
            if resident["id"] in {item["sourceResidentId"], item["targetResidentId"]}
        ]
    if len(components) > 1:
        changes.append(
            f"connected {len(components)} resident relationship components with "
            f"{len(components) - 1} minimal bridge(s)"
        )

    for event in payload["events"]:
        event["participantResidentIds"] = _unique_strings(event.get("participantResidentIds"), valid_residents)
        event["locationIds"] = _unique_strings(event.get("locationIds"), valid_locations)
        event["revealedFactIds"] = _unique_strings(event.get("revealedFactIds"), valid_facts)

    project = payload["changeProjects"][0]
    conversions = {
        "Purpose.Home": "Purpose.Clinic",
        "Purpose.Workplace": "Purpose.Shelter",
        "Purpose.Shelter": "Purpose.Headquarters",
        "Purpose.Headquarters": "Purpose.Workplace",
    }
    eligible = [item for item in locations if item.get("bRepurposable") and item.get("purposeTag") in conversions]
    target = location_by_id.get(project.get("targetLocationId"))
    if target not in eligible:
        target = eligible[0] if eligible else None
    if target is None:
        raise ValueError("population has no location eligible for a supported change project")
    project["targetLocationId"] = target["id"]
    project["desiredPurposeTag"] = conversions[target["purposeTag"]]
    if project.get("initiatorResidentId") not in valid_residents:
        project["initiatorResidentId"] = resident_ids[0]
    participants = _unique_strings(project.get("requiredParticipantResidentIds"), valid_residents)
    for required in (project["initiatorResidentId"], target.get("ownerResidentId"), target.get("controllerResidentId")):
        if required in valid_residents and required not in participants:
            participants.append(required)
    project["requiredParticipantResidentIds"] = participants
    if supported:
        project["requiredCapabilityTags"] = _unique_strings(project.get("requiredCapabilityTags"), supported)
    project["requiredConditionTags"] = [
        "Condition.ThreatActive", "Condition.Overnight", "Condition.PlayerAway"
    ]
    project["intendedStartMinute"] = max(1200, int(project.get("intendedStartMinute", 1200)))
    project["requiredTransitionMinutes"] = int(_clamp(project.get("requiredTransitionMinutes", 360), 60, 1440))
    project["state"] = "Proposed"
    payload["changeProjects"] = [project]
    return changes


def validate_population_semantics(request: dict, response: dict) -> None:
    if request.get("stage") != "population":
        return
    payload = response["payload"]
    current = request.get("current", {})
    locations = current.get("locations", [])
    facts = current.get("facts", [])
    location_by_id = {item["id"]: item for item in locations}
    resident_by_id = {item["id"]: item for item in payload["residents"]}
    household_by_id = {item["id"]: item for item in payload["households"]}
    relationship_by_id = {item["id"]: item for item in payload["relationships"]}
    belief_by_id = {item["id"]: item for item in payload["beliefs"]}
    fact_ids = {item["id"] for item in facts}
    if len(resident_by_id) != len(payload["residents"]):
        raise ValueError("population resident IDs must be unique")
    resident_ids = set(resident_by_id)
    for location in locations:
        for field in ("ownerResidentId", "controllerResidentId"):
            value = location.get(field, "")
            if value and value not in resident_ids:
                raise ValueError(f"location authority {value!r} is absent from the population")
    for resident in payload["residents"]:
        if resident["homeLocationId"] not in location_by_id or resident["currentLocationId"] not in location_by_id:
            raise ValueError(f"resident {resident['id']} references an unknown location")
        household = household_by_id.get(resident["householdId"])
        if not household or household["homeLocationId"] != resident["homeLocationId"] or resident["id"] not in household["memberResidentIds"]:
            raise ValueError(f"resident {resident['id']} has inconsistent household membership")
        if any(item["factId"] not in fact_ids for item in resident["importantMemories"]):
            raise ValueError(f"resident {resident['id']} has a memory outside the closed fact set")
        if any(item not in belief_by_id for item in resident["beliefIds"]):
            raise ValueError(f"resident {resident['id']} references an unknown belief")
        if any(item not in relationship_by_id for item in resident["relationshipIds"]):
            raise ValueError(f"resident {resident['id']} references an unknown relationship")
    for household in payload["households"]:
        home = location_by_id.get(household["homeLocationId"])
        if not home or len(household["memberResidentIds"]) > int(home.get("residentCapacity", 0)):
            raise ValueError(f"household {household['id']} exceeds its certified home capacity")
    adjacency = {resident_id: set() for resident_id in resident_ids}
    for relationship in payload["relationships"]:
        source = relationship["sourceResidentId"]
        target = relationship["targetResidentId"]
        if source not in resident_ids or target not in resident_ids or source == target:
            raise ValueError(f"relationship {relationship['id']} has invalid endpoints")
        adjacency[source].add(target)
        adjacency[target].add(source)
        if relationship["bBidirectional"]:
            reciprocal = relationship_by_id.get(relationship["reciprocalRelationshipId"])
            if not reciprocal or reciprocal["sourceResidentId"] != target or reciprocal["targetResidentId"] != source:
                raise ValueError(f"relationship {relationship['id']} has an invalid reciprocal")
    visited: set[str] = set()
    pending = [next(iter(resident_ids))]
    while pending:
        current_id = pending.pop()
        if current_id in visited:
            continue
        visited.add(current_id)
        pending.extend(adjacency[current_id] - visited)
    if visited != resident_ids:
        raise ValueError("resident relationship network is disconnected")
    for belief in payload["beliefs"]:
        if belief["holderResidentId"] not in resident_ids or belief["factId"] not in fact_ids:
            raise ValueError(f"belief {belief['id']} has invalid references")
    covered_fact_ids = {belief["factId"] for belief in payload["beliefs"]}
    orphaned_secrets = [
        fact["id"] for fact in facts
        if fact.get("bSecret") and fact["id"] not in covered_fact_ids
    ]
    if orphaned_secrets:
        raise ValueError(f"secret facts have no resident belief holders: {orphaned_secrets}")
    if len(payload["changeProjects"]) != 1 or payload["changeProjects"][0]["state"] != "Proposed":
        raise ValueError("population requires exactly one Proposed change project")


def normalize_and_validate_stage_response(request: dict, response: dict) -> tuple[dict, list[str]]:
    if not isinstance(response, dict) or response.get("stage") != request.get("stage"):
        raise ValueError("agent response has the wrong or non-canonical stage envelope")
    if not isinstance(response.get("payload"), dict):
        raise ValueError("agent response payload must be an object")
    changes = normalize_brief_response(request, response)
    changes.extend(normalize_topology_response(request, response))
    changes.extend(normalize_population_response(request, response))
    validate_stage_response(request, response)
    validate_brief_semantics(request, response)
    validate_topology_semantics(request, response)
    validate_population_semantics(request, response)
    return response, changes


FIRST_NAMES = (
    "Mara", "Ivo", "Anwen", "Tomas", "Nia", "Corin", "Elian", "Sera", "Bryn", "Lina",
    "Orin", "Mae", "Dara", "Eira", "Cal", "Rhea", "Jonas", "Talia", "Oren", "Vera",
    "Ari", "Mira", "Cade", "Esme", "Theo", "Lysa", "Ren", "Ada", "Finn", "Noa",
    "Hale", "Isolde", "Bram", "Wren", "Petra", "Silas", "Odette", "Garrick", "Yara", "Emrys",
    "Tamsin", "Rook", "Liesl", "Benedek", "Saoirse", "Kestrel", "Ilse", "Torin", "Junia", "Aldo",
    "Fen", "Marit", "Osric", "Delphine", "Huxley", "Rowena", "Taddeo", "Brannagh", "Quill", "Solveig",
)
FAMILY_NAMES = (
    "Vale", "Reed", "Pike", "Wren", "Alder", "Ash", "Moss", "Finch", "Holt", "Ford",
    "Bell", "Rowan", "Flint", "Fen", "Thorn", "Brook", "Gale", "Hart", "Lark", "Dunn",
    "Stone", "Fenn", "Willow", "Rill", "Birch", "Crane", "Hawke", "Cove", "Marsh", "Ember",
    "Cotter", "Hollis", "Mercer", "Sedge", "Tanner", "Ostler", "Larch", "Quarry", "Weir", "Sallow",
)
NON_PERSON_ID_TOKENS = {
    "resident", "generated", "authority", "owner", "controller", "council", "town", "village",
    "guild", "house", "hall", "the", "of", "district", "ward", "location", "loc", "people",
    "folk", "community", "keepers", "keeper", "watch", "crew", "family", "clan", "elders",
    "office", "collective", "board", "company", "trust", "order", "circle", "assembly",
    "commons", "society", "brotherhood", "sisterhood", "league", "cooperative", "coop",
}
OCCUPATION_KEYWORDS = (
    ("Occupation.Farmer", ("farm", "field", "orchard", "granary", "grain", "mill", "barn", "pasture",
                           "garden", "harvest", "paddy", "flax", "vine", "herd", "fold", "grove")),
    ("Occupation.Merchant", ("market", "trade", "store", "shop", "inn", "tavern", "ferry", "harbor",
                             "dock", "quay", "caravan", "exchange", "wharf", "landing", "toll", "post")),
    ("Occupation.Guard", ("watch", "gate", "guard", "tower", "beacon", "fort", "lookout", "barracks",
                          "wall", "patrol", "garrison", "avalanche", "bell")),
    ("Occupation.Herbalist", ("herb", "clinic", "heal", "apothecary", "infirmary", "shrine", "spring",
                              "temple", "chapel", "physic", "remedy", "bath")),
)
MOTIVATION_TEMPLATES = {
    "Occupation.Reeve": (
        "Hold {settlement} together through what is coming: {threat}.",
        "Settle the quarrel before it splits {settlement}: {tension}.",
        "Make sure {settlement} never repeats its old mistake: {wound}.",
    ),
    "Occupation.Farmer": (
        "Get every row at {workplace} harvested before the danger reaches it.",
        "Prove that {workplace} can still feed every household in {settlement}.",
        "Store enough from {workplace} that no family in {district} goes hungry.",
    ),
    "Occupation.Merchant": (
        "Keep goods moving through {workplace} whatever comes: {threat}.",
        "Turn the town's trouble into fair trade at {workplace}, not profiteering.",
        "Clear what {workplace} owes before the season turns.",
    ),
    "Occupation.Guard": (
        "Spot the danger first from {workplace} and buy {settlement} time to act.",
        "Keep the ways past {workplace} open for everyone who has to leave.",
        "Win back the trust the watch lost after {wound}.",
    ),
    "Occupation.Herbalist": (
        "Have remedies ready at {workplace} before the injured arrive.",
        "Find what the old stories say about the danger and use it.",
        "Keep the frightened and the sick calm at {workplace}.",
    ),
    "Occupation.Worker": (
        "Keep {workplace} running so the rest of {settlement} can rely on it.",
        "Finish the repairs at {workplace} before they are needed.",
        "Earn a voice in how {settlement} meets the danger: {threat}.",
    ),
}
FEAR_TEMPLATES = (
    "That {home} will be the first to suffer: {threat}.",
    "That the quarrel will turn neighbours against each other: {tension}.",
    "That {settlement} will forget its own warning: {wound}.",
    "That {workplace} will be abandoned exactly when it matters.",
    "That help will come too late for {district}.",
    "That the secret everyone half-knows will come out at the worst moment.",
    "That {leader} is hiding how bad things already are.",
)
MEMORY_TEMPLATES = (
    "Remembers how it started: {statement}",
    "Was there when it became plain: {statement}",
    "Heard it from {other} and never forgot: {statement}",
    "Still argues about it at supper: {statement}",
)
PROJECT_PURPOSE_NOUNS = {
    "Purpose.Clinic": "temporary clinic",
    "Purpose.Shelter": "public shelter",
    "Purpose.Headquarters": "response headquarters",
    "Purpose.Workplace": "working hall",
}


def _seeded_random(seed: object, salt: str) -> random.Random:
    digest = hashlib.sha256(f"{seed}|{salt}".encode("utf-8")).digest()
    return random.Random(int.from_bytes(digest[:8], "big"))


def _clause(text: object, fallback: str, limit: int = 120) -> str:
    """One readable clause from model-written prose, for template interpolation.

    Only the first sentence is used, and it is shortened at a clause or word boundary,
    so a template never ends mid-thought ("...drowning cultivated.").
    """
    value = re.sub(r"\s+", " ", str(text or "")).strip()
    value = re.split(r"(?<=[.!?;])\s+", value, maxsplit=1)[0].strip().rstrip(".!?;:")
    if not value:
        return fallback
    if len(value) > limit:
        shortened = value[:limit]
        cut = max(shortened.rfind(", "), shortened.rfind(" and "), shortened.rfind(" while "),
                  shortened.rfind(" as "), shortened.rfind(" because "), shortened.rfind(" but "))
        if cut > limit // 3:
            value = shortened[:cut]
        else:
            # No clause boundary: end on a word, never on a dangling function word.
            words = shortened.rsplit(" ", 1)[0].split(" ")
            while words and words[-1].lower() in {"to", "the", "a", "an", "of", "and", "for", "with", "in", "on", "at", "by"}:
                words.pop()
            value = " ".join(words)
        value = value.rstrip(",;: ")
    if len(value) > 1 and value[0].isupper() and not value[1].isupper():
        value = value[0].lower() + value[1:]
    return value


ROLE_ID_TOKENS = {
    "reeve", "merchant", "guard", "herbalist", "farmer", "worker", "captain", "elder", "keeper",
    "smith", "warden", "healer", "miller", "ferryman", "boatwright", "wright", "priest",
    "priestess", "mayor", "steward", "sheriff", "constable", "bailiff", "harbor", "harbormaster",
    "master", "mistress", "chief", "head", "lord", "lady", "sir", "dame", "old", "young",
}


def _person_name_from_id(resident_id: str) -> str | None:
    """Use an authority ID as a name only when it clearly names a person.

    resident.elian.vesper -> "Elian Vesper"; resident.reeve.mara -> "Mara". Role-only,
    institutional, or run-together IDs (resident.reevemara) return None so a generated
    name is used instead of a mangled one.
    """
    match = re.match(r"^(?:resident|person|npc)[._-](.+)$", resident_id, flags=re.IGNORECASE)
    if not match:
        return None
    tokens = [token for token in re.split(r"[._\-\s]+", match.group(1)) if token]
    if any(not token.isalpha() or token.lower() in NON_PERSON_ID_TOKENS for token in tokens):
        return None
    tokens = [token for token in tokens if token.lower() not in ROLE_ID_TOKENS]
    if not 1 <= len(tokens) <= 3 or any(len(token) > 10 for token in tokens):
        return None
    known_first_names = {name.lower() for name in FIRST_NAMES}
    if len(tokens) == 1 and len(tokens[0]) > 7 and tokens[0].lower() not in known_first_names:
        return None
    return " ".join(token.capitalize() for token in tokens)


def _occupation_for(location: dict, supported: set[str]) -> str:
    identity = f"{location.get('id', '')} {location.get('displayName', '')}".lower()
    choice = "Occupation.Worker"
    if location.get("purposeTag") == "Purpose.Clinic":
        choice = "Occupation.Herbalist"
    for occupation, words in OCCUPATION_KEYWORDS:
        if any(word in identity for word in words):
            choice = occupation
            break
    if supported and choice not in supported:
        return "Occupation.Worker" if "Occupation.Worker" in supported else next(
            (tag for tag in sorted(supported) if tag.startswith("Occupation.")), choice
        )
    return choice


def _bidirectional_pair(
    relationships: list[dict], linked: set[frozenset], source: str, target: str,
    relationship_type: str, scores: dict, key: str,
) -> None:
    pair = frozenset((source, target))
    if source == target or pair in linked:
        return
    linked.add(pair)
    forward_id = f"relationship.{key}.forward"
    reverse_id = f"relationship.{key}.reverse"
    for (first, second, own_id, other_id) in (
        (source, target, forward_id, reverse_id),
        (target, source, reverse_id, forward_id),
    ):
        relationships.append({
            "version": 1,
            "id": own_id,
            "sourceResidentId": first,
            "targetResidentId": second,
            "relationshipType": relationship_type,
            "bBidirectional": True,
            "reciprocalRelationshipId": other_id,
            **scores,
        })


def synthesized_population_response(request: dict) -> dict:
    """Build the social layer locally, deterministically, from the accepted topology.

    No model call is made. Every name, occupation, motivation, household, rivalry, and
    history event is derived from this world's topology, facts, and seed, so two worlds
    with different premises produce different towns.
    """
    current = request.get("current", {})
    current = current if isinstance(current, dict) else {}
    locations = copy.deepcopy(current.get("locations", []))
    facts = copy.deepcopy(current.get("facts", []))
    threats = current.get("threats", []) if isinstance(current.get("threats"), list) else []
    topology = current.get("topology", {}) if isinstance(current.get("topology"), dict) else {}
    brief = current.get("brief", {}) if isinstance(current.get("brief"), dict) else {}
    if not locations or not facts:
        raise ValueError("local population synthesis requires accepted locations and facts")
    seed = request.get("seed", current.get("seed", 0))
    rng = _seeded_random(seed, "population")
    supported = _supported_tags(request)
    location_by_id = {item["id"]: item for item in locations if isinstance(item.get("id"), str)}
    homes = [item for item in locations if item.get("purposeTag") == "Purpose.Home"]
    workplaces = [item for item in locations if item.get("purposeTag") != "Purpose.Home"]
    if not homes:
        raise ValueError("local population synthesis requires at least one home")
    if not workplaces:
        workplaces = list(homes)

    settlement = _clause(brief.get("settlementIdentity"), "the settlement", 48)
    if settlement and settlement[0].islower():
        settlement = settlement[0].upper() + settlement[1:]
    settlement = settlement.split(",")[0].split(" is ")[0].strip() or "the settlement"
    # Models sometimes put a threat ID in centralThreat; prose reads better from the premise.
    threat_text = str(topology.get("centralThreat") or "")
    if " " not in threat_text.strip() and threats:
        threat_text = str(threats[0].get("premise") or threat_text)
    clauses = {
        "settlement": settlement,
        "threat": _clause(threat_text, "the danger at the edge of town"),
        "tension": _clause(topology.get("currentTension"), "who should bear the cost of staying"),
        "wound": _clause(topology.get("historicalWound"), "the year the town nearly failed"),
    }

    # Residents: every named authority first, then enough others for a seeded 20-30 town.
    authority_ids = _unique_strings([
        value
        for location in locations
        for value in (location.get("ownerResidentId"), location.get("controllerResidentId"))
        if isinstance(value, str) and value
    ])[:30]
    home_capacity = sum(max(0, int(item.get("residentCapacity", 0))) for item in homes)
    # The model often states the population ("a town of 24 residents"); use it so the
    # synthesized town does not contradict the world's own facts.
    stated_text = " ".join(
        [str(item) for item in brief.get("premises", [])] +
        [str(item.get("statement", "")) for item in facts]
    ).lower()
    word_numbers = {
        "twenty": 20, "twenty-one": 21, "twenty-two": 22, "twenty-three": 23, "twenty-four": 24,
        "twenty-five": 25, "twenty-six": 26, "twenty-seven": 27, "twenty-eight": 28,
        "twenty-nine": 29, "thirty": 30,
    }
    stated_count = None
    match = re.search(r"\b(\d{2}|twenty(?:-\w+)?|thirty)\s+(?:residents|people|villagers|souls|inhabitants)", stated_text)
    if match:
        token = match.group(1)
        stated_count = int(token) if token.isdigit() else word_numbers.get(token)
    if stated_count is not None and 20 <= stated_count <= 30:
        target_count = max(len(authority_ids), min(home_capacity, stated_count))
    else:
        target_count = max(len(authority_ids), min(30, home_capacity, 20 + rng.randint(0, 10)))
    target_count = max(20, min(30, target_count))
    resident_ids = list(authority_ids)
    while len(resident_ids) < target_count:
        candidate = f"resident.generated.{len(resident_ids) + 1:02d}"
        if candidate not in resident_ids:
            resident_ids.append(candidate)

    # Homes: owners live in the homes they own; everyone else fills homes in proportion
    # to capacity so household sizes vary instead of a fixed round-robin.
    remaining = {item["id"]: max(0, int(item.get("residentCapacity", 0))) for item in homes}
    home_of: dict[str, str] = {}
    for home in homes:
        for authority in (home.get("ownerResidentId"), home.get("controllerResidentId")):
            if authority in resident_ids and authority not in home_of and remaining[home["id"]] > 0:
                home_of[authority] = home["id"]
                remaining[home["id"]] -= 1
    unhoused = [item for item in resident_ids if item not in home_of]
    rng.shuffle(unhoused)
    for resident_id in unhoused:
        open_homes = [home_id for home_id, slots in remaining.items() if slots > 0]
        if not open_homes:
            raise ValueError("topology home capacity is smaller than the synthesized population")
        weights = [remaining[home_id] for home_id in open_homes]
        home_id = rng.choices(open_homes, weights=weights, k=1)[0]
        home_of[resident_id] = home_id
        remaining[home_id] -= 1

    # Names: person-like authority IDs keep their names; households share a surname.
    surnames = list(FAMILY_NAMES)
    rng.shuffle(surnames)
    surname_of_home = {home["id"]: surnames[index % len(surnames)] for index, home in enumerate(homes)}
    first_names = list(FIRST_NAMES)
    rng.shuffle(first_names)
    display_name: dict[str, str] = {}
    used_first: set[str] = set()
    for resident_id in resident_ids:
        derived = _person_name_from_id(resident_id)
        if derived:
            parts = derived.split()
            used_first.add(parts[0])
            display_name[resident_id] = derived if len(parts) > 1 else f"{derived} {surname_of_home[home_of[resident_id]]}"
    for resident_id in resident_ids:
        if resident_id in display_name:
            continue
        first = next((name for name in first_names if name not in used_first), None)
        if first is None:
            first = rng.choice(FIRST_NAMES)
        used_first.add(first)
        display_name[resident_id] = f"{first} {surname_of_home[home_of[resident_id]]}"

    # Leader: whoever the governance text names; otherwise the authority of the
    # headquarters or landmark.
    governance = str(topology.get("governance", "")).lower()
    leader = next(
        (rid for rid in resident_ids if display_name[rid].lower() in governance),
        None,
    ) or next(
        (rid for rid in resident_ids
         if re.search(rf"\b{re.escape(display_name[rid].split()[0].lower())}\b", governance)),
        None,
    )
    if leader is None:
        for purpose in ("Purpose.Headquarters", "Purpose.Landmark"):
            authority = next((
                item.get("controllerResidentId") or item.get("ownerResidentId")
                for item in locations
                if item.get("purposeTag") == purpose and
                (item.get("controllerResidentId") or item.get("ownerResidentId")) in resident_ids
            ), None)
            if authority:
                leader = authority
                break
    leader = leader or resident_ids[0]

    # Workplaces: authorities work where they hold authority; others spread so every
    # workplace is staffed before any gets a second worker.
    workplace_of: dict[str, str] = {}
    for location in workplaces:
        for authority in (location.get("controllerResidentId"), location.get("ownerResidentId")):
            if authority in resident_ids and authority not in workplace_of:
                workplace_of[authority] = location["id"]
    headquarters = next((item for item in locations if item.get("purposeTag") == "Purpose.Headquarters"), None)
    if headquarters is not None:
        workplace_of[leader] = headquarters["id"]
    staffed = {item["id"]: 0 for item in workplaces}
    for workplace_id in workplace_of.values():
        if workplace_id in staffed:
            staffed[workplace_id] += 1
    unassigned = [item for item in resident_ids if item not in workplace_of]
    rng.shuffle(unassigned)
    for resident_id in unassigned:
        fewest = min(staffed.values())
        choices = sorted(item for item, count in staffed.items() if count == fewest)
        workplace_id = rng.choice(choices)
        workplace_of[resident_id] = workplace_id
        staffed[workplace_id] += 1

    # Models often write "Sunledge Ward: clustered homes on the sunny flank"; keep the name.
    district_names = _unique_strings([
        re.split(r"\s*(?::|—|–| - |\()", str(item), maxsplit=1)[0].strip()
        for item in topology.get("districts", [])
    ]) or [settlement]
    district_of_home = {home["id"]: district_names[index % len(district_names)] for index, home in enumerate(homes)}
    occupation_of = {
        rid: ("Occupation.Reeve" if rid == leader and (not supported or "Occupation.Reeve" in supported)
              else _occupation_for(location_by_id[workplace_of[rid]], supported))
        for rid in resident_ids
    }

    # Beliefs and memories: everyone remembers a public fact; secrets stay with the
    # leader and the few people closest to the place they concern.
    public_facts = [item for item in facts if not item.get("bSecret")] or facts
    secret_facts = [item for item in facts if item.get("bSecret")]
    beliefs: list[dict] = []
    memories_of: dict[str, list[dict]] = {rid: [] for rid in resident_ids}
    beliefs_of: dict[str, list[str]] = {rid: [] for rid in resident_ids}

    def add_belief(holder: str, fact: dict, secret: bool, source: str = "") -> None:
        belief_id = f"belief.{len(beliefs) + 1:03d}"
        beliefs.append({
            "version": 1,
            "id": belief_id,
            "holderResidentId": holder,
            "factId": fact["id"],
            "confidence": round(rng.uniform(0.62, 0.95), 2),
            "bImportantSecret": bool(secret and fact.get("bEstablished")),
            "sourceResidentId": source,
            "secrecy": round(rng.uniform(0.7, 0.92) if secret else rng.uniform(0.08, 0.35), 2),
            "emotionalSignificance": round(rng.uniform(0.45, 0.9), 2),
            "willingnessToShare": round(rng.uniform(0.1, 0.32) if secret else rng.uniform(0.55, 0.9), 2),
        })
        beliefs_of[holder].append(belief_id)

    for index, rid in enumerate(resident_ids):
        fact = public_facts[(index + rng.randint(0, len(public_facts) - 1)) % len(public_facts)]
        other = display_name[resident_ids[(index + 3) % len(resident_ids)]].split()[0]
        template = MEMORY_TEMPLATES[rng.randrange(len(MEMORY_TEMPLATES))]
        memories_of[rid].append({
            "id": f"memory.{index + 1:02d}.1",
            "factId": fact["id"],
            "summary": template.format(statement=str(fact.get("statement", "")).strip(), other=other),
            "day": max(0, int(fact.get("establishedDay", 0))),
            "emotionalSignificance": round(rng.uniform(0.4, 0.9), 2),
        })
        add_belief(rid, fact, False)
    for fact_index, fact in enumerate(secret_facts):
        holders = [leader]
        affected = next(
            (item for item in threats if item.get("affectedLocationIds")), {}
        ).get("affectedLocationIds", [])
        for location_id in affected:
            authority = (location_by_id.get(location_id) or {}).get("controllerResidentId")
            if authority in resident_ids and authority not in holders:
                holders.append(authority)
                break
        holders.append(resident_ids[(fact_index * 7 + 5) % len(resident_ids)])
        for holder in dict.fromkeys(holders):
            add_belief(holder, fact, True, source="" if holder == leader else leader)
            memories_of[holder].append({
                "id": f"memory.secret.{fact_index + 1}.{len(memories_of[holder]) + 1}",
                "factId": fact["id"],
                "summary": f"Keeps this to themself: {str(fact.get('statement', '')).strip()}",
                "day": max(0, int(fact.get("establishedDay", 0))),
                "emotionalSignificance": round(rng.uniform(0.7, 0.95), 2),
            })

    residents = []
    for index, rid in enumerate(resident_ids):
        home = location_by_id[home_of[rid]]
        workplace = location_by_id[workplace_of[rid]]
        occupation = occupation_of[rid]
        values = {
            **clauses,
            "workplace": workplace.get("displayName") or workplace["id"],
            "home": home.get("displayName") or home["id"],
            "district": district_of_home[home["id"]],
            "leader": display_name[leader] if rid != leader else "the council",
        }
        motivations = MOTIVATION_TEMPLATES.get(occupation, MOTIVATION_TEMPLATES["Occupation.Worker"])
        residents.append({
            "version": 1,
            "id": rid,
            "displayName": display_name[rid],
            "homeLocationId": home["id"],
            "workplaceLocationId": workplace["id"],
            "householdId": f"household.{home['id']}",
            "occupationTag": occupation,
            "bEmployed": True,
            "currentLocationId": home["id"],
            "motivation": motivations[rng.randrange(len(motivations))].format(**values),
            "fear": FEAR_TEMPLATES[rng.randrange(len(FEAR_TEMPLATES))].format(**values),
            "importantMemories": memories_of[rid],
            "availability": "Available",
            "beliefIds": beliefs_of[rid],
            "relationshipIds": [],
        })
    resident_by_id = {item["id"]: item for item in residents}

    households = []
    for home in homes:
        members = [rid for rid in resident_ids if home_of[rid] == home["id"]]
        if members:
            households.append({
                "version": 1,
                "id": f"household.{home['id']}",
                "homeLocationId": home["id"],
                "memberResidentIds": members,
            })

    # Relationships: family chains, coworker chains, the leader's ties to other
    # authorities, and a few cross-district rivalries that carry the town's tension.
    relationships: list[dict] = []
    linked: set[frozenset] = set()

    def scores(trust: tuple, affinity: tuple, fear: tuple, obligation: tuple) -> dict:
        return {
            "trust": round(rng.uniform(*trust), 2),
            "affinity": round(rng.uniform(*affinity), 2),
            "fear": round(rng.uniform(*fear), 2),
            "obligation": round(rng.uniform(*obligation), 2),
        }

    for household in households:
        members = household["memberResidentIds"]
        for index in range(len(members) - 1):
            _bidirectional_pair(relationships, linked, members[index], members[index + 1],
                                "Relationship.Family", scores((0.72, 0.92), (0.65, 0.92), (0.0, 0.12), (0.6, 0.88)),
                                f"family.{len(linked) + 1:03d}")
    by_workplace: dict[str, list[str]] = {}
    for rid in resident_ids:
        by_workplace.setdefault(workplace_of[rid], []).append(rid)
    for workplace_id in sorted(by_workplace):
        members = by_workplace[workplace_id]
        for index in range(len(members) - 1):
            _bidirectional_pair(relationships, linked, members[index], members[index + 1],
                                "Relationship.Coworker", scores((0.55, 0.78), (0.4, 0.7), (0.02, 0.18), (0.3, 0.55)),
                                f"coworker.{len(linked) + 1:03d}")
    for authority in [item for item in authority_ids if item != leader][:6]:
        _bidirectional_pair(relationships, linked, leader, authority,
                            "Relationship.Authority", scores((0.45, 0.72), (0.3, 0.6), (0.12, 0.35), (0.55, 0.85)),
                            f"authority.{len(linked) + 1:03d}")
    rivalry_target = 2 + rng.randint(0, 2)
    attempts = 0
    rivalries = 0
    while rivalries < rivalry_target and attempts < 200:
        attempts += 1
        first, second = rng.sample(resident_ids, 2)
        if home_of[first] == home_of[second] or frozenset((first, second)) in linked:
            continue
        _bidirectional_pair(relationships, linked, first, second,
                            "Relationship.Rival", scores((0.12, 0.35), (0.08, 0.3), (0.3, 0.55), (0.05, 0.25)),
                            f"rival.{len(linked) + 1:03d}")
        rivalries += 1
    for resident in residents:
        resident["relationshipIds"] = [
            item["id"] for item in relationships
            if resident["id"] in {item["sourceResidentId"], item["targetResidentId"]}
        ]

    # History: the established public facts become dated events with witnesses, and
    # the current tension becomes the most recent meeting at the landmark.
    landmark_ids = [item for item in topology.get("landmarkLocationIds", []) if item in location_by_id]
    landmark_id = landmark_ids[0] if landmark_ids else locations[0]["id"]
    events = []
    dated = sorted(
        (item for item in public_facts if item.get("bEstablished")),
        key=lambda item: int(item.get("establishedDay", 0)),
    )[:2]
    for index, fact in enumerate(dated):
        witnesses = [item["holderResidentId"] for item in beliefs if item["factId"] == fact["id"]][:3]
        events.append({
            "version": 1,
            "id": f"event.history.{index + 1}",
            "day": max(0, int(fact.get("establishedDay", 0))),
            "summary": str(fact.get("statement", "")).strip() or "The town remembers this day.",
            "participantResidentIds": witnesses,
            "locationIds": [landmark_id],
            "revealedFactIds": [fact["id"]],
        })
    latest_day = max([int(item.get("establishedDay", 0)) for item in facts] + [0])
    rivals = [item for item in relationships if item["relationshipType"] == "Relationship.Rival"]
    events.append({
        "version": 1,
        "id": "event.recent_dispute",
        "day": latest_day,
        "summary": f"A heated meeting at {location_by_id[landmark_id].get('displayName', landmark_id)}: "
                   f"{clauses['tension']}.",
        "participantResidentIds": list(dict.fromkeys(
            [leader] + ([rivals[0]["sourceResidentId"], rivals[0]["targetResidentId"]] if rivals else [])
        )),
        "locationIds": [landmark_id],
        "revealedFactIds": [],
    })

    # Change project: adapt a building the central threat actually touches.
    conversions = {
        "Purpose.Home": "Purpose.Clinic",
        "Purpose.Workplace": "Purpose.Shelter",
        "Purpose.Shelter": "Purpose.Headquarters",
        "Purpose.Headquarters": "Purpose.Workplace",
    }
    affected_ids = {location_id for item in threats for location_id in item.get("affectedLocationIds", [])}
    eligible = [
        item for item in locations
        if item.get("bRepurposable") and item.get("purposeTag") in conversions
    ] or [item for item in locations if item.get("purposeTag") in conversions]
    if not eligible:
        raise ValueError("local population synthesis requires a convertible location")
    target = next((item for item in eligible if item["id"] in affected_ids), eligible[0])
    initiator = next(
        (item for item in (target.get("controllerResidentId"), target.get("ownerResidentId"))
         if item in resident_by_id),
        leader,
    )
    desired = conversions[target["purposeTag"]]
    project_capabilities = {
        "Purpose.Clinic": ["Capability.Bed", "Capability.Counter", "Capability.Door", "Capability.Interior"],
        "Purpose.Shelter": ["Capability.Bed", "Capability.Chair", "Capability.Door", "Capability.Interior"],
        "Purpose.Headquarters": ["Capability.WorkSurface", "Capability.Chair", "Capability.Door", "Capability.Interior"],
        "Purpose.Workplace": ["Capability.WorkSurface", "Capability.Counter", "Capability.Door", "Capability.Interior"],
    }
    required_capabilities = project_capabilities[desired]
    if supported:
        required_capabilities = [item for item in required_capabilities if item in supported]
    response = {
        "stage": "population",
        "payload": {
            "residents": residents,
            "households": households,
            "relationships": relationships,
            "beliefs": beliefs,
            "events": events,
            "changeProjects": [{
                "version": 1,
                "id": "project.generated.adaptation",
                "initiatorResidentId": initiator,
                "targetLocationId": target["id"],
                "desiredPurposeTag": desired,
                "reason": (
                    f"Turn {target.get('displayName') or target['id']} into a "
                    f"{PROJECT_PURPOSE_NOUNS[desired]} to answer the danger: {clauses['threat']}."
                ),
                "requiredParticipantResidentIds": list(dict.fromkeys([initiator, leader])),
                "requiredCapabilityTags": required_capabilities,
                "requiredConditionTags": [
                    "Condition.ThreatActive", "Condition.Overnight", "Condition.PlayerAway"
                ],
                "intendedStartMinute": 1200,
                "requiredTransitionMinutes": rng.choice((240, 300, 360, 420)),
                "state": "Proposed",
            }],
        },
    }
    response, changes = normalize_and_validate_stage_response(request, response)
    response["diagnostics"] = {
        "requestedModel": "deterministic local population synthesizer",
        "reasoningEffort": "n/a",
        "companionOutcome": "success",
        "parseSuccess": True,
        "schemaSuccess": True,
        "inputTokens": 0,
        "cachedInputTokens": 0,
        "outputTokens": 0,
        "reasoningOutputTokens": 0,
        "normalizationChanges": changes,
        "costNote": "No model call was made for the population layer; it is derived from the topology.",
    }
    return response


def synthesized_population_repair_response(request: dict) -> dict:
    population = synthesized_population_response({**request, "stage": "population"})
    replacements = [
        {"section": section, "value": copy.deepcopy(population["payload"][section])}
        for section in STAGE_KEYS["population"]
    ]
    response = {"stage": "repair", "payload": {"replacements": replacements}}
    validate_stage_response(request, response)
    response["diagnostics"] = population["diagnostics"]
    return response


def cli_response(request: dict, output_path: Path) -> dict:
    configured = os.environ.get("WORLD_DIRECTOR_CLI_COMMAND", "").strip()
    requested_model = str(request.get("model", "")).strip()
    reasoning_effort = str(request.get("reasoningEffort", "")).strip().lower()
    if requested_model and not re.fullmatch(r"[A-Za-z0-9._-]+", requested_model):
        raise ValueError("model contains unsupported characters")
    if reasoning_effort not in {"", "low", "medium", "high", "xhigh", "max", "ultra"}:
        raise ValueError("unsupported reasoning effort")
    structured_output = not configured
    if configured:
        command = shlex.split(configured)
        command = [
            argument.replace("{model}", requested_model).replace(
                "{reasoning}", reasoning_effort
            )
            for argument in command
        ]
        # A configured provider opts into structured output by accepting {schema}.
        structured_output = any("{schema}" in argument for argument in command)
    else:
        codex = discover_codex()
        if not codex:
            raise RuntimeError(
                "No configured CLI agent. Set WORLD_DIRECTOR_CLI_COMMAND or install codex "
                "in PATH, /opt/homebrew/bin, /usr/local/bin, ~/.local/bin, or a standard "
                "macOS Codex/ChatGPT application location."
            )
        validate_model_choice(requested_model, reasoning_effort)
        # The agent runs in an empty working directory with every tool-bearing feature
        # disabled and no skill catalog, and the stage schema is enforced by the
        # provider's structured output. Auth still comes from the user's CODEX_HOME.
        command = [
            codex,
            "--ask-for-approval",
            "never",
            "--sandbox",
            "read-only",
            "exec",
            "--ignore-user-config",
            "--ignore-rules",
            "--ephemeral",
            "--skip-git-repo-check",
            "--cd",
            "{workdir}",
            "--color",
            "never",
            "--json",
            "--output-schema",
            "{schema}",
            "--output-last-message",
            "{output}",
            "--config",
            "skills.include_instructions=false",
            "--config",
            "cloud.skills.enabled=false",
            "--config",
            "tools.update_plan.enabled=false",
        ]
        for feature in CODEX_DISABLED_FEATURES:
            command.extend(["--disable", feature])
        command.append("-")
        if requested_model:
            command[1:1] = ["--model", requested_model]
        if reasoning_effort:
            command[1:1] = [
                "--config",
                f'model_reasoning_effort="{reasoning_effort}"',
            ]

    base_prompt = build_agent_prompt(request, include_schema=not structured_output)
    output_schema_json = json.dumps(
        provider_output_schema(stage_response_schema(request)), separators=(",", ":")
    )
    if len(base_prompt.encode("utf-8")) > MAX_REQUEST_BYTES:
        raise ValueError(
            f"constructed prompt exceeds the {MAX_REQUEST_BYTES}-byte local CLI budget"
        )
    prompt_path = artifact_path(output_path, "prompt.txt")
    raw_response_path = artifact_path(output_path, "raw-response.txt")
    events_path = artifact_path(output_path, "provider-events.jsonl")
    telemetry_path = artifact_path(output_path, "telemetry.json")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    provider_started = time.perf_counter()
    timeout_seconds = max(
        1.0,
        float(request.get("companionTimeoutSeconds", DEFAULT_COMPANION_TIMEOUT_SECONDS)),
    )
    deadline = provider_started + timeout_seconds
    try:
        provider_attempt_limit = int(
            os.environ.get("WORLD_DIRECTOR_PROVIDER_MAX_ATTEMPTS")
            or request.get("providerMaxAttempts", DEFAULT_PROVIDER_ATTEMPTS)
        )
    except (TypeError, ValueError):
        provider_attempt_limit = DEFAULT_PROVIDER_ATTEMPTS
    provider_attempt_limit = max(1, min(3, provider_attempt_limit))
    previous_attempt_seconds = 0.0
    attempt_records: list[dict] = []
    totals = {
        "inputTokens": 0,
        "cachedInputTokens": 0,
        "outputTokens": 0,
        "reasoningOutputTokens": 0,
    }
    correction = ""
    last_error: Exception | None = None
    global _ACTIVE_AGENT_PROCESS, _ACTIVE_TELEMETRY_PATH

    for provider_attempt in range(1, provider_attempt_limit + 1):
        remaining = deadline - time.perf_counter()
        if remaining <= 0.0:
            last_error = RuntimeError(
                f"CLI agent exhausted the companion timeout of {timeout_seconds:.1f} seconds."
            )
            break
        # A corrective retry that cannot finish only converts a clear validation
        # error into a timeout. Require roughly the time the failed attempt took.
        if provider_attempt > 1 and remaining < max(MINIMUM_RETRY_SECONDS, previous_attempt_seconds):
            last_error = RuntimeError(
                f"{last_error}; only {remaining:.0f}s of the stage budget remained, "
                "not enough for a corrective retry"
            )
            break
        prompt = base_prompt + correction
        if len(prompt.encode("utf-8")) > MAX_REQUEST_BYTES:
            raise ValueError(
                f"corrective prompt exceeds the {MAX_REQUEST_BYTES}-byte local CLI budget"
            )
        prompt_path.write_text(prompt + "\n", encoding="utf-8")
        attempt_prompt_path = artifact_path(
            output_path, f"provider-attempt-{provider_attempt}-prompt.txt"
        )
        attempt_prompt_path.write_text(prompt + "\n", encoding="utf-8")
        attempt_started = time.perf_counter()
        with tempfile.TemporaryDirectory(prefix="world-director-") as temp_dir:
            agent_output = Path(temp_dir) / "agent-response.json"
            schema_path = Path(temp_dir) / "output-schema.json"
            schema_path.write_text(output_schema_json, encoding="utf-8")
            work_directory = Path(temp_dir) / "workdir"
            work_directory.mkdir()
            expanded = [
                arg.replace("{output}", str(agent_output))
                .replace("{schema}", str(schema_path))
                .replace("{workdir}", str(work_directory))
                for arg in command
            ]
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
                creationflags=(
                    getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
                    if os.name == "nt" else 0
                ),
            )
            _ACTIVE_AGENT_PROCESS = process
            try:
                try:
                    stdout, stderr = process.communicate(input=prompt, timeout=remaining)
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
            stdout_text = stdout or ""
            raw_response = agent_output.read_text(encoding="utf-8") if agent_output.exists() else ""
            # Codex emits JSONL telemetry on stdout and writes the answer to {output}.
            # A provider-neutral sandbox command may instead emit only its answer on stdout.
            provider_events = stdout_text if raw_response else ""
            if not raw_response and configured:
                raw_response = stdout_text

        previous_attempt_seconds = time.perf_counter() - attempt_started
        events_path.write_text(provider_events, encoding="utf-8")
        raw_response_path.write_text(raw_response, encoding="utf-8")
        attempt_events_path = artifact_path(
            output_path, f"provider-attempt-{provider_attempt}-events.jsonl"
        )
        attempt_raw_path = artifact_path(
            output_path, f"provider-attempt-{provider_attempt}-raw-response.txt"
        )
        attempt_events_path.write_text(provider_events, encoding="utf-8")
        attempt_raw_path.write_text(raw_response, encoding="utf-8")
        usage = parse_provider_events(provider_events)
        for key in totals:
            totals[key] += int(usage.get(key, 0) or 0)
        record = {
            "providerAttempt": provider_attempt,
            "durationSeconds": time.perf_counter() - attempt_started,
            "exitCode": return_code,
            "timedOut": timed_out,
            "promptCharacters": len(prompt),
            "responseCharacters": len(raw_response),
            "threadId": usage.get("threadId", ""),
            "inputTokens": usage.get("inputTokens", 0),
            "cachedInputTokens": usage.get("cachedInputTokens", 0),
            "outputTokens": usage.get("outputTokens", 0),
            "reasoningOutputTokens": usage.get("reasoningOutputTokens", 0),
            "promptPath": str(attempt_prompt_path),
            "rawResponsePath": str(attempt_raw_path),
            "providerEventsPath": str(attempt_events_path),
            "providerStderr": (stderr or "")[-8000:],
            "parseSuccess": False,
            "schemaSuccess": False,
            "normalizationChanges": [],
        }

        try:
            if timed_out:
                raise RuntimeError(
                    f"CLI agent exceeded the companion timeout of {timeout_seconds:.1f} seconds."
                )
            if return_code != 0:
                raise RuntimeError(
                    f"configured CLI agent exited {return_code}: {(stderr or '')[-2000:]}"
                )
            if not raw_response:
                raise RuntimeError("configured CLI agent produced no last-message file")
            if len(raw_response.encode("utf-8")) > MAX_RESPONSE_BYTES:
                raise ValueError(
                    f"response exceeds the {MAX_RESPONSE_BYTES}-byte local CLI budget"
                )
            response = strict_json_loads(strip_code_fence(raw_response))
            record["parseSuccess"] = True
            response, normalization_changes = normalize_and_validate_stage_response(
                request, response
            )
            record["schemaSuccess"] = True
            record["normalizationChanges"] = normalization_changes
            record["outcome"] = "success"
            attempt_records.append(record)
            telemetry = {
                **usage,
                **totals,
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
                "timedOut": False,
                "timeoutSeconds": timeout_seconds,
                "structuredOutput": structured_output,
                "parseSuccess": True,
                "schemaSuccess": True,
                "providerAttemptCount": provider_attempt,
                "providerAttempts": attempt_records,
                "normalizationChanges": normalization_changes,
                "companionOutcome": "success",
                "billedCostUsd": None,
                "costNote": (
                    "Unavailable: the Codex CLI reports tokens but does not emit a "
                    "monetary charge for this authenticated run."
                ),
            }
            telemetry_path.write_text(
                json.dumps(telemetry, indent=2) + "\n", encoding="utf-8"
            )
            response["diagnostics"] = telemetry
            return response
        except Exception as exc:
            last_error = exc
            record["outcome"] = (
                "provider_timeout" if timed_out else
                "provider_error" if return_code != 0 else
                "response_validation_error"
            )
            record["error"] = str(exc)[:2000]
            attempt_records.append(record)
            failure_telemetry = {
                **usage,
                **totals,
                "runId": str(request.get("runId", "")),
                "stage": str(request.get("stage", "")),
                "attempt": int(request.get("attempt", 0) or 0),
                "requestedModel": requested_model or "CLI default (not reported)",
                "reasoningEffort": reasoning_effort or "CLI default",
                "providerDurationSeconds": time.perf_counter() - provider_started,
                "promptPath": str(prompt_path),
                "rawResponsePath": str(raw_response_path),
                "providerEventsPath": str(events_path),
                "telemetryPath": str(telemetry_path),
                "exitCode": return_code,
                "timedOut": timed_out,
                "timeoutSeconds": timeout_seconds,
                "structuredOutput": structured_output,
                "parseSuccess": record["parseSuccess"],
                "schemaSuccess": False,
                "providerAttemptCount": provider_attempt,
                "providerAttempts": attempt_records,
                "companionOutcome": record["outcome"],
                "responseValidationError": str(exc)[:2000],
                "billedCostUsd": None,
                "costNote": (
                    "Unavailable: the Codex CLI reports tokens but does not emit a "
                    "monetary charge for this authenticated run."
                ),
            }
            telemetry_path.write_text(
                json.dumps(failure_telemetry, indent=2) + "\n", encoding="utf-8"
            )
            if provider_attempt >= provider_attempt_limit or timed_out:
                break
            correction = (
                "\n\nCORRECTIVE RETRY: The previous provider response was rejected before "
                "it reached Unreal. Regenerate the complete stage response from scratch. "
                "Do not copy the invalid response. Correct this exact failure: "
                f"{str(exc)[:1000]}"
            )

    if last_error is None:
        last_error = RuntimeError("configured CLI agent failed without an explicit error")
    raise last_error


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
    provider_mode = request.get("providerMode")
    if provider_mode == "fixture":
        response = fixture_response(request)
    elif provider_mode == "synthesized" and stage == "population":
        response = synthesized_population_response(request)
    elif provider_mode == "synthesized" and stage == "repair" and request.get("populationBundleRepair"):
        response = synthesized_population_repair_response(request)
    else:
        response = cli_response(request, args.output)
    temp_output = args.output.with_suffix(args.output.suffix + ".tmp")
    temp_output.write_text(
        json.dumps(response, separators=(",", ":")) + "\n", encoding="utf-8"
    )
    temp_output.replace(args.output)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # Companion errors are reported through process status and log.
        print(f"world_director_companion: {exc}", file=sys.stderr)
        raise SystemExit(1)
