"""Model-free contract tests for the World Director companion bridge."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest


COMPANION_PATH = (
    Path(__file__).resolve().parents[1]
    / "Plugins"
    / "WorldDirector"
    / "Resources"
    / "Companion"
    / "world_director_companion.py"
)
SPEC = importlib.util.spec_from_file_location("world_director_companion", COMPANION_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"Could not load companion module from {COMPANION_PATH}")
COMPANION = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(COMPANION)


def request_for(stage: str) -> dict:
    candidates = [
        {
            "opaqueId": "candidate-a",
            "summary": "river terrace with a direct civic approach",
            "selected": True,
        },
        {
            "opaqueId": "candidate-b",
            "summary": "broken ridge with switchback roads",
            "selected": False,
        },
    ]
    return {
        "stage": stage,
        "runId": "offline-contract-test",
        "attempt": 0,
        "playerPrompt": "A wind-cut river settlement divided over an old oath.",
        "seed": 42,
        "providerMode": "fixture",
        "capabilitySummary": {
            "supportedPurposeTags": ["Purpose.Home", "Purpose.Workplace"],
            "supportedActivityTags": ["Activity.Sleep", "Activity.Work"],
        },
        "current": {
            "id": "world.contract_test",
            "seed": 42,
            "brief": {},
            "topology": {},
            "locations": [],
            "facts": [],
            "threats": [],
        },
        "layoutCandidates": candidates,
        "worldContext": {
            "runId": "offline-contract-test",
            "originalRootSeed": 42,
            "selectedLayoutSeed": 42,
            "selectedCandidateId": "candidate-a",
            "selectedPhysicalLayout": candidates[0],
            "creativePillars": {"theme": "contested river oath"},
            "lorePillars": {"historicalWound": "the bridge compact failed"},
        },
        "validationIssues": [],
    }


class CompanionContractTests(unittest.TestCase):
    def test_live_prompts_are_neutral_valid_and_bounded(self) -> None:
        budgets = {
            "interpret": 5_000,
            "topology": 8_000,
            "layout": 6_000,
            "population": 11_000,
            "repair": 7_000,
        }
        for stage, budget in budgets.items():
            with self.subTest(stage=stage):
                request = request_for(stage)
                prompt = COMPANION.build_agent_prompt(request)
                self.assertLess(len(prompt), budget)
                self.assertNotIn("Brackenford", prompt)
                self.assertNotIn("living-town", prompt)
                self.assertNotIn("canonical shape exemplar", prompt.lower())
                envelope = COMPANION.response_envelope_example(stage)
                self.assertEqual(json.loads(json.dumps(envelope)), envelope)
                schema = COMPANION.stage_response_schema(request)
                self.assertEqual(schema["properties"]["stage"]["const"], stage)
                self.assertFalse(schema["additionalProperties"])

    def test_strict_json_rejects_ambiguous_provider_output(self) -> None:
        with self.assertRaisesRegex(ValueError, "duplicate JSON key"):
            COMPANION.strict_json_loads('{"stage":"layout","stage":"repair"}')
        with self.assertRaisesRegex(ValueError, "non-standard JSON number"):
            COMPANION.strict_json_loads('{"value":NaN}')
        with self.assertRaisesRegex(ValueError, "one JSON object"):
            COMPANION.strict_json_loads("[]")

    def test_layout_response_must_choose_one_supplied_candidate(self) -> None:
        request = request_for("layout")
        accepted = {
            "stage": "layout",
            "payload": {"selectedCandidateId": "candidate-a"},
        }
        self.assertIs(COMPANION.validate_stage_response(request, accepted), accepted)
        accepted["payload"]["selectedCandidateId"] = "candidate-missing"
        with self.assertRaisesRegex(ValueError, "unknown candidate"):
            COMPANION.validate_stage_response(request, accepted)

    def test_stage_payload_types_and_extra_keys_are_rejected(self) -> None:
        request = request_for("interpret")
        with self.assertRaisesRegex(ValueError, "payload keys differ"):
            COMPANION.validate_stage_response(
                request,
                {"stage": "interpret", "payload": {"brief": {}, "comment": "no"}},
            )
        with self.assertRaisesRegex(ValueError, "must be an object"):
            COMPANION.validate_stage_response(
                request,
                {"stage": "interpret", "payload": {"brief": []}},
            )

    def test_population_schema_rejects_string_resident_memories(self) -> None:
        request = request_for("population")
        response = COMPANION.fixture_response(request)
        response["payload"]["residents"][0]["importantMemories"] = [
            "memory_flooded_ferry"
        ]
        with self.assertRaisesRegex(ValueError, "no allowed schema variant|must be an object"):
            COMPANION.validate_stage_response(request, response)

    def test_population_schema_rejects_missing_current_location(self) -> None:
        request = request_for("population")
        response = COMPANION.fixture_response(request)
        response["payload"]["residents"][0].pop("currentLocationId")
        with self.assertRaisesRegex(ValueError, "missing required"):
            COMPANION.validate_stage_response(request, response)

    def test_population_schema_requires_social_state(self) -> None:
        request = request_for("population")
        response = COMPANION.fixture_response(request)
        response["payload"]["residents"][0]["importantMemories"] = []
        response["payload"]["residents"][0]["beliefIds"] = []
        response["payload"]["residents"][0]["relationshipIds"] = []
        with self.assertRaisesRegex(ValueError, "array has too few items"):
            COMPANION.validate_stage_response(request, response)

    def test_repair_prompt_preserves_all_distinct_issue_classes(self) -> None:
        request = request_for("repair")
        request["validationIssues"] = [
            {
                "code": "slice.resident_social_state",
                "path": "$.residents[0]",
                "message": "Every resident requires memories, beliefs, and at least one relationship.",
            },
            {
                "code": "slice.resident_social_state",
                "path": "$.residents[1]",
                "message": "Every resident requires memories, beliefs, and at least one relationship.",
            },
            {
                "code": "reference.missing",
                "path": "$.residents[0].importantMemories[0].factId",
                "message": "Referenced fact ID 'fact.missing' does not exist.",
            },
        ]
        compact = COMPANION.compact_validation_issues(request)
        self.assertEqual(len(compact), 2)
        prompt = COMPANION.build_agent_prompt(request)
        self.assertIn("fact.missing", prompt)
        self.assertIn("exactly one replacement for every supplied", prompt)

    def test_repair_schema_is_typed_to_the_indexed_target(self) -> None:
        request = request_for("repair")
        request["repairTargets"] = [{"section": "residents", "index": 0}]
        response = COMPANION.fixture_response(
            {
                **request,
                "validationIssues": [
                    {"path": "$.residents[0].importantMemories"}
                ],
            }
        )
        self.assertIs(COMPANION.validate_stage_response(request, response), response)
        response["payload"]["replacements"][0]["value"]["importantMemories"] = [
            "memory_flooded_ferry"
        ]
        with self.assertRaisesRegex(ValueError, "no allowed schema variant"):
            COMPANION.validate_stage_response(request, response)

    def test_provider_event_telemetry_is_explicit_about_missing_usage(self) -> None:
        events = "\n".join(
            [
                '{"type":"thread.started","thread_id":"thread-123"}',
                "not-json",
                '{"type":"turn.completed","usage":{"input_tokens":120,"output_tokens":30}}',
            ]
        )
        telemetry = COMPANION.parse_provider_events(events)
        self.assertEqual(telemetry["threadId"], "thread-123")
        self.assertEqual(telemetry["eventCount"], 2)
        self.assertEqual(telemetry["malformedEventCount"], 1)
        self.assertTrue(telemetry["usageAvailable"])
        self.assertEqual(telemetry["inputTokens"], 120)
        self.assertEqual(telemetry["outputTokens"], 30)

    def test_cli_timeout_writes_bounded_telemetry(self) -> None:
        request = request_for("interpret")
        request["companionTimeoutSeconds"] = 0.1
        previous = os.environ.get("WORLD_DIRECTOR_CLI_COMMAND")
        os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = (
            f"{sys.executable} -c 'import time; time.sleep(2)'"
        )
        try:
            with tempfile.TemporaryDirectory() as directory:
                output_path = Path(directory) / "interpret-response.json"
                with self.assertRaisesRegex(RuntimeError, "timeout"):
                    COMPANION.cli_response(request, output_path)
                telemetry_path = output_path.with_name("interpret-telemetry.json")
                telemetry = json.loads(telemetry_path.read_text(encoding="utf-8"))
                self.assertEqual(telemetry["companionOutcome"], "provider_timeout")
                self.assertTrue(telemetry["timedOut"])
        finally:
            if previous is None:
                os.environ.pop("WORLD_DIRECTOR_CLI_COMMAND", None)
            else:
                os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = previous


if __name__ == "__main__":
    unittest.main()
