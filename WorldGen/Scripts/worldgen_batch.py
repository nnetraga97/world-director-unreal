#!/usr/bin/env python3
"""Run the World Director semantic pipeline headlessly and report reliability.

Unreal is not involved: this drives the same companion code Unreal launches for the
interpret and topology stages, then the local population synthesizer, and applies the
companion's full schema and semantic validation. It measures what fails, how often,
and how long each stage takes. Physical terrain/layout generation still runs inside
Unreal (WorldDirector.WorldGeneration.* automation tests cover it).

Modes:
  --replay-corpus   Re-validate the saved real model outputs in
                    Resources/Fixtures/model-output-corpus.json. No model calls.
  --live            Call the configured provider for each prompt. Every prompt costs
                    at least two model calls (interpret + topology), more on retries.

Examples:
  python3 WorldGen/Scripts/worldgen_batch.py --replay-corpus
  python3 WorldGen/Scripts/worldgen_batch.py --live --prompt "A salt-marsh ferry town" \\
      --model gpt-6.1-sol --effort medium --report /tmp/batch.json
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
COMPANION_PATH = ROOT / "Plugins" / "WorldDirector" / "Resources" / "Companion" / "world_director_companion.py"
CORPUS_PATH = ROOT / "Plugins" / "WorldDirector" / "Resources" / "Fixtures" / "model-output-corpus.json"
SUPPORTED_TAGS = sorted({
    "Purpose.Home", "Purpose.Workplace", "Purpose.Landmark", "Purpose.Clinic", "Purpose.Shelter",
    "Purpose.Headquarters", "Occupation.Farmer", "Occupation.Worker", "Occupation.Merchant",
    "Occupation.Guard", "Occupation.Reeve", "Occupation.Herbalist", "Capability.Bed",
    "Capability.WorkSurface", "Capability.Chair", "Capability.Counter", "Capability.Door",
    "Capability.Interior", "Capability.Landmark", "Capability.Character.Modular",
})


def load_companion():
    spec = importlib.util.spec_from_file_location("world_director_companion", COMPANION_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {COMPANION_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def base_request(stage: str, prompt: str, seed: int, model: str, effort: str, timeout: float) -> dict:
    return {
        "stage": stage,
        "runId": f"batch-{seed}",
        "attempt": 0,
        "playerPrompt": prompt,
        "seed": seed,
        "model": model,
        "reasoningEffort": effort,
        "providerMode": "cli",
        "companionTimeoutSeconds": timeout,
        "capabilitySummary": {
            "theme": "Asset-led stylized village and rural frontier",
            "supportedTags": SUPPORTED_TAGS,
            "residentMinimum": 20,
            "residentMaximum": 30,
            "certifiedPlotMinimum": 12,
            "certifiedPlotMaximum": 18,
            "homePlotMaximum": 6,
        },
        "current": {"id": f"world.generated_{seed}", "seed": seed},
        "layoutCandidates": [],
        "worldContext": {"originalRootSeed": seed},
        "validationIssues": [],
        "repairTargets": [],
    }


def finish_world(companion, seed: int, brief: dict, topology_payload: dict) -> dict:
    """Normalize the topology and synthesize the population exactly as Unreal would."""
    topology_request = base_request("topology", "", seed, "", "", 0)
    topology_request["current"]["brief"] = brief
    topology, topology_changes = companion.normalize_and_validate_stage_response(
        topology_request, {"stage": "topology", "payload": json.loads(json.dumps(topology_payload))}
    )
    payload = topology["payload"]
    population_request = base_request("population", "", seed, "", "", 0)
    population_request["current"].update({
        "brief": brief,
        **{key: payload[key] for key in ("topology", "locations", "facts", "threats")},
    })
    population = companion.synthesized_population_response(population_request)
    residents = population["payload"]["residents"]
    return {
        "landform": brief.get("landform", ""),
        "presentation": brief.get("presentation", {}),
        "locations": len(payload["locations"]),
        "residents": len(residents),
        "occupations": sorted({item["occupationTag"] for item in residents}),
        "relationshipTypes": sorted({
            item["relationshipType"] for item in population["payload"]["relationships"]
        }),
        "events": len(population["payload"]["events"]),
        "topologyNormalizations": topology_changes,
    }


def replay_corpus(companion) -> list[dict]:
    results = []
    for entry in json.loads(CORPUS_PATH.read_text(encoding="utf-8"))["entries"]:
        result = {"runId": entry["runId"], "expectedLandform": entry["expectedLandform"]}
        try:
            result.update(finish_world(companion, int(entry["seed"]), entry["brief"], entry["topology"]))
            result["outcome"] = "valid"
        except Exception as error:  # noqa: BLE001 - every failure class is reported
            result["outcome"] = "invalid"
            result["error"] = str(error)
        results.append(result)
    return results


def run_live(companion, prompts: list[str], model: str, effort: str, timeout: float) -> list[dict]:
    results = []
    for index, prompt in enumerate(prompts):
        seed = 50_000 + index
        result: dict = {"prompt": prompt, "seed": seed, "stages": []}
        started = time.perf_counter()
        with tempfile.TemporaryDirectory(prefix="worldgen-batch-") as directory:
            try:
                interpret = base_request("interpret", prompt, seed, model, effort, timeout)
                response = timed_stage(companion, interpret, Path(directory) / "01-interpret-response.json", result)
                brief = response["payload"]["brief"]
                topology = base_request("topology", prompt, seed, model, effort, timeout)
                topology["current"]["brief"] = brief
                response = timed_stage(companion, topology, Path(directory) / "02-topology-response.json", result)
                result.update(finish_world(companion, seed, brief, response["payload"]))
                result["outcome"] = "valid"
            except Exception as error:  # noqa: BLE001 - every failure class is reported
                result["outcome"] = "failed"
                result["error"] = str(error)[:2000]
        result["elapsedSeconds"] = round(time.perf_counter() - started, 1)
        results.append(result)
        print(json.dumps({key: result.get(key) for key in ("seed", "outcome", "elapsedSeconds", "landform", "presentation", "error")}))
    return results


def timed_stage(companion, request: dict, output: Path, result: dict) -> dict:
    started = time.perf_counter()
    try:
        response = companion.cli_response(request, output)
    except Exception:
        result["stages"].append({"stage": request["stage"], "seconds": round(time.perf_counter() - started, 1),
                                 "outcome": "failed"})
        raise
    diagnostics = response.get("diagnostics", {})
    result["stages"].append({
        "stage": request["stage"],
        "seconds": round(time.perf_counter() - started, 1),
        "outcome": "success",
        "providerAttempts": diagnostics.get("providerAttemptCount"),
        "inputTokens": diagnostics.get("inputTokens"),
        "outputTokens": diagnostics.get("outputTokens"),
    })
    return response


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--replay-corpus", action="store_true")
    mode.add_argument("--live", action="store_true")
    parser.add_argument("--prompt", action="append", default=[], help="player prompt (repeatable)")
    parser.add_argument("--prompts-file", type=Path, help="one prompt per line")
    parser.add_argument("--model", default="")
    parser.add_argument("--effort", default="medium")
    parser.add_argument("--stage-timeout", type=float, default=295.0)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    companion = load_companion()
    if args.replay_corpus:
        results = replay_corpus(companion)
    else:
        prompts = list(args.prompt)
        if args.prompts_file:
            prompts.extend(line.strip() for line in args.prompts_file.read_text(encoding="utf-8").splitlines() if line.strip())
        if not prompts:
            parser.error("--live needs at least one --prompt or --prompts-file")
        results = run_live(companion, prompts, args.model, args.effort, args.stage_timeout)

    valid = sum(item.get("outcome") == "valid" for item in results)
    summary = {"total": len(results), "valid": valid, "results": results}
    if args.replay_corpus:
        # Corpus briefs predate brief.landform; Unreal resolves those from prose, which the
        # WorldDirector.WorldGeneration.LandformFromModelCorpus automation test checks.
        with_landform = [item for item in results if item.get("landform")]
        summary["briefsWithLandform"] = len(with_landform)
        summary["landformMatches"] = sum(
            item.get("landform") == item.get("expectedLandform") for item in with_landform
        )
    text = json.dumps(summary, indent=2)
    if args.report:
        args.report.write_text(text + "\n", encoding="utf-8")
    print(json.dumps({key: summary[key] for key in summary if key != "results"}))
    return 0 if valid == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
