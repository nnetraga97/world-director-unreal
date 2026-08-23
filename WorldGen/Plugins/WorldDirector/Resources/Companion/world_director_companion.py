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
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


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
    current = request.get("current", {})
    candidates = request.get("layoutCandidates", [])
    validation = request.get("validationIssues", [])
    exemplar_request = dict(request)
    exemplar_request["providerMode"] = "fixture"
    exemplar_request["testInvalidPopulationOnce"] = False
    exemplar = fixture_response(exemplar_request)
    required = ", ".join(STAGE_KEYS[stage])
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
            "array. Do not regenerate valid sections."
        )
        if request.get("replaceRepeatedFailure"):
            repair_rule += (
                " A prior targeted repair repeated a validation failure. Replace the invalid "
                "detail with the closest simpler supported alternative instead of preserving it."
            )
    return f"""
You are the World Director semantic generator. Complete only stage '{stage}'.
{blank_rule}
{repair_rule}
Never emit Unreal asset paths, transforms, coordinates, commentary, markdown, or questions.
Return exactly one JSON object with this shape:
{{"stage":"{stage}","payload":{{{required}}}}}
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

Player prompt: {request.get('playerPrompt', '')!r}
Seed: {request.get('seed', 0)}
Capability summary:
{json.dumps(capability_summary, indent=2, sort_keys=True)}
Accepted stages so far:
{json.dumps(current, indent=2, sort_keys=True)}
Opaque Unreal layout candidates:
{json.dumps(candidates, indent=2, sort_keys=True)}
Targeted validation issues:
{json.dumps(validation, indent=2, sort_keys=True)}
Canonical shape exemplar (copy its exact fields and value types, but create content that fits the accepted stages):
{json.dumps(exemplar, indent=2, sort_keys=True)}
""".strip()


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
    }
    for line in events_text.splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("type") == "thread.started":
            telemetry["threadId"] = str(event.get("thread_id", ""))
        if event.get("type") != "turn.completed":
            continue
        usage = event.get("usage", {})
        if not isinstance(usage, dict):
            continue
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
            telemetry[camel_name] += int(usage.get(wire_name, 0) or 0)
    return telemetry


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
    prompt_path = artifact_path(output_path, "prompt.txt")
    raw_response_path = artifact_path(output_path, "raw-response.txt")
    events_path = artifact_path(output_path, "provider-events.jsonl")
    telemetry_path = artifact_path(output_path, "telemetry.json")
    prompt_path.write_text(prompt + "\n", encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="world-director-") as temp_dir:
        agent_output = Path(temp_dir) / "agent-response.json"
        expanded = [arg.replace("{output}", str(agent_output)) for arg in command]
        completed = subprocess.run(
            expanded,
            input=prompt,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=agent_environment(command[0]),
            check=False,
        )
        provider_events = completed.stdout or ""
        events_path.write_text(provider_events, encoding="utf-8")
        raw_response = ""
        if agent_output.exists():
            raw_response = agent_output.read_text(encoding="utf-8")
            raw_response_path.write_text(raw_response, encoding="utf-8")
        telemetry = parse_provider_events(provider_events)
        telemetry.update(
            {
                "requestedModel": requested_model or "CLI default (not reported)",
                "reasoningEffort": reasoning_effort or "CLI default",
                "promptCharacters": len(prompt),
                "promptPath": str(prompt_path),
                "rawResponsePath": str(raw_response_path),
                "providerEventsPath": str(events_path),
                "telemetryPath": str(telemetry_path),
                "exitCode": completed.returncode,
                "providerStderr": (completed.stderr or "")[-8000:],
                "billedCostUsd": None,
                "costNote": (
                    "Unavailable: the Codex CLI reports tokens but does not emit a "
                    "monetary charge for this authenticated run."
                ),
            }
        )
        telemetry_path.write_text(
            json.dumps(telemetry, indent=2) + "\n", encoding="utf-8"
        )
        if completed.returncode != 0:
            raise RuntimeError(
                f"configured CLI agent exited {completed.returncode}: "
                f"{completed.stderr[-2000:]}"
            )
        if not raw_response:
            raise RuntimeError("configured CLI agent produced no last-message file")
        response = json.loads(strip_code_fence(raw_response))

    if not isinstance(response, dict) or response.get("stage") != request["stage"]:
        raise ValueError("agent response has the wrong stage envelope")
    payload = response.get("payload")
    if not isinstance(payload, dict):
        raise ValueError("agent response payload must be an object")
    for key in STAGE_KEYS[request["stage"]]:
        if key not in payload:
            raise ValueError(f"agent response payload is missing '{key}'")
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

    response = (
        fixture_response(request)
        if request.get("providerMode") == "fixture"
        else cli_response(request, args.output)
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
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
