"""Model-free contract tests for the World Director companion bridge."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
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


if __name__ == "__main__":
    unittest.main()
