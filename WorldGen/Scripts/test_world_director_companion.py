"""Model-free contract tests for the World Director companion bridge."""

from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import textwrap
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
FIXTURE_PATH = (
    Path(__file__).resolve().parents[1]
    / "Plugins"
    / "WorldDirector"
    / "Resources"
    / "Fixtures"
    / "living-town.json"
)


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


def integrated_request_for(stage: str) -> dict:
    fixture = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    request = request_for(stage)
    request["current"] = {
        key: fixture[key]
        for key in ("id", "seed", "brief", "topology", "locations", "facts", "threats")
    }
    request["capabilitySummary"] = {"supportedTags": sorted({
        "Purpose.Home",
        "Purpose.Workplace",
        "Purpose.Landmark",
        "Purpose.Clinic",
        "Purpose.Shelter",
        "Purpose.Headquarters",
        "Occupation.Farmer",
        "Occupation.Worker",
        "Occupation.Merchant",
        "Occupation.Guard",
        "Occupation.Reeve",
        "Occupation.Herbalist",
        "Capability.Bed",
        "Capability.WorkSurface",
        "Capability.Chair",
        "Capability.Counter",
        "Capability.Door",
        "Capability.Interior",
        "Capability.Landmark",
        "Capability.Character.Modular",
    })}
    return request


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

    def test_provider_json_collapses_only_identical_duplicate_values(self) -> None:
        response, changes = COMPANION.provider_json_loads(
            '{"stage":"interpret","payload":{"brief":{"id":"same","id":"same"}}}'
        )
        self.assertEqual(response["payload"]["brief"]["id"], "same")
        self.assertEqual(changes, ["Collapsed identical duplicate JSON key 'id'."])
        with self.assertRaisesRegex(ValueError, "conflicting duplicate JSON key"):
            COMPANION.provider_json_loads(
                '{"stage":"interpret","stage":"repair"}'
            )

    def test_provider_json_removes_only_bounded_dangling_property_marker(self) -> None:
        response, changes = COMPANION.provider_json_loads(
            '{"stage":"interpret","payload":{"brief":{"id":"same","}}}'
        )
        self.assertEqual(response["payload"]["brief"]["id"], "same")
        self.assertEqual(
            changes,
            ["Removed dangling quoted property marker before object close."],
        )
        with self.assertRaises(json.JSONDecodeError):
            COMPANION.provider_json_loads('{"stage" "interpret"}')

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

    def test_topology_normalization_guarantees_capacity_and_project_target(self) -> None:
        request = integrated_request_for("topology")
        response = COMPANION.fixture_response(request)
        for location in response["payload"]["locations"]:
            if location["purposeTag"] == "Purpose.Home":
                location["residentCapacity"] = 1
            location["bRepurposable"] = False
        response, changes = COMPANION.normalize_and_validate_stage_response(request, response)
        homes = [
            item for item in response["payload"]["locations"]
            if item["purposeTag"] == "Purpose.Home"
        ]
        self.assertGreaterEqual(sum(item["residentCapacity"] for item in homes), 30)
        self.assertTrue(any(item["bRepurposable"] for item in response["payload"]["locations"]))
        self.assertTrue(changes)

    def test_topology_authorities_are_collapsed_to_population_limit(self) -> None:
        request = integrated_request_for("topology")
        response = COMPANION.fixture_response(request)
        for index, location in enumerate(response["payload"]["locations"]):
            location["ownerResidentId"] = "resident." if index == 0 else f"resident.owner.{index}"
            location["controllerResidentId"] = f"resident.controller.{index}"
        response, changes = COMPANION.normalize_and_validate_stage_response(request, response)
        authority_ids = {
            authority
            for location in response["payload"]["locations"]
            for authority in (location["ownerResidentId"], location["controllerResidentId"])
            if authority
        }
        self.assertLessEqual(len(authority_ids), 30)
        self.assertTrue(any("overflow authority" in change for change in changes))

        population_request = integrated_request_for("population")
        for key in ("topology", "locations", "facts", "threats"):
            population_request["current"][key] = response["payload"][key]
        population = COMPANION.synthesized_population_response(population_request)
        COMPANION.validate_population_semantics(population_request, population)
        self.assertTrue(all(
            resident["displayName"].strip()
            for resident in population["payload"]["residents"]
        ))

    def test_population_normalization_repairs_historical_cross_reference_failures(self) -> None:
        request = integrated_request_for("population")
        request["current"]["locations"][0]["ownerResidentId"] = "resident.required_authority"
        response = COMPANION.fixture_response(request)
        payload = response["payload"]
        payload["residents"][0]["currentLocationId"] = "location.missing"
        payload["residents"][0]["importantMemories"][0]["factId"] = "threat.not_a_fact"
        payload["residents"][1]["beliefIds"] = []
        payload["residents"][2]["relationshipIds"] = ["relationship.missing"]
        payload["households"][0]["memberResidentIds"] = [
            item["id"] for item in payload["residents"]
        ]
        payload["relationships"] = payload["relationships"][:2]
        payload["beliefs"][0]["factId"] = "fact.missing"
        payload["changeProjects"][0].update({
            "initiatorResidentId": "resident.missing",
            "targetLocationId": "location.missing",
            "desiredPurposeTag": "Purpose.Unsupported",
            "requiredParticipantResidentIds": ["resident.missing"],
            "requiredConditionTags": ["Condition.Unsupported"],
            "intendedStartMinute": 1,
            "requiredTransitionMinutes": 1,
            "state": "Active",
        })
        response, changes = COMPANION.normalize_and_validate_stage_response(request, response)
        self.assertTrue(changes)
        self.assertIn(
            "resident.required_authority",
            {item["id"] for item in response["payload"]["residents"]},
        )
        self.assertLess(len(json.dumps(response).encode("utf-8")), COMPANION.MAX_RESPONSE_BYTES)

    def test_population_normalization_survives_mutated_edge_cases(self) -> None:
        base_request = integrated_request_for("population")
        for mutation in range(60):
            with self.subTest(mutation=mutation):
                request = json.loads(json.dumps(base_request))
                response = COMPANION.fixture_response(request)
                payload = response["payload"]
                resident = payload["residents"][mutation % len(payload["residents"])]
                resident["currentLocationId"] = f"location.missing.{mutation}"
                resident["beliefIds"] = []
                resident["relationshipIds"] = []
                resident["importantMemories"][0]["factId"] = f"fact.missing.{mutation}"
                payload["relationships"] = payload["relationships"][: mutation % 9 + 1]
                payload["households"] = payload["households"][: mutation % 3 + 1]
                payload["beliefs"] = payload["beliefs"][: mutation % 5 + 1]
                payload["changeProjects"][0]["targetLocationId"] = "location.missing"
                normalized, _ = COMPANION.normalize_and_validate_stage_response(request, response)
                self.assertLess(
                    len(json.dumps(normalized).encode("utf-8")),
                    COMPANION.MAX_RESPONSE_BYTES,
                )

    def test_provider_retries_malformed_json_then_accepts_valid_response(self) -> None:
        request = request_for("interpret")
        request["providerMode"] = "cli"
        request["providerMaxAttempts"] = 2
        request["companionTimeoutSeconds"] = 10
        valid = COMPANION.fixture_response(request)
        previous = os.environ.get("WORLD_DIRECTOR_CLI_COMMAND")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "provider.py"
            counter = root / "counter.txt"
            script.write_text(
                textwrap.dedent(
                    """
                    import json
                    from pathlib import Path
                    import sys
                    counter = Path(sys.argv[1])
                    output = Path(sys.argv[2])
                    count = int(counter.read_text() or "0") if counter.exists() else 0
                    counter.write_text(str(count + 1))
                    if count == 0:
                        output.write_text('{"stage":"interpret","stage":"repair"}')
                    else:
                        output.write_text(sys.stdin.read())
                    print(json.dumps({"type":"thread.started","thread_id":f"attempt-{count + 1}"}))
                    """
                ),
                encoding="utf-8",
            )
            # The second attempt reads the complete valid JSON from stdin. Use a tiny wrapper
            # prompt-independent command so this remains a provider-boundary test.
            valid_path = root / "valid.json"
            valid_path.write_text(json.dumps(valid), encoding="utf-8")
            script.write_text(
                script.read_text(encoding="utf-8").replace(
                    "output.write_text(sys.stdin.read())",
                    f"output.write_text(Path({str(valid_path)!r}).read_text())",
                ),
                encoding="utf-8",
            )
            os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = (
                f"{sys.executable} {script} {counter} {{output}}"
            )
            try:
                output_path = root / "interpret-response.json"
                response = COMPANION.cli_response(request, output_path)
                self.assertEqual(response["stage"], "interpret")
                self.assertEqual(response["diagnostics"]["providerAttemptCount"], 2)
                self.assertEqual(
                    response["diagnostics"]["providerAttempts"][0]["outcome"],
                    "response_validation_error",
                )
            finally:
                if previous is None:
                    os.environ.pop("WORLD_DIRECTOR_CLI_COMMAND", None)
                else:
                    os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = previous

    def test_configured_provider_may_return_response_on_stdout(self) -> None:
        request = request_for("interpret")
        request["providerMode"] = "cli"
        request["companionTimeoutSeconds"] = 10
        valid = COMPANION.fixture_response(request)
        previous = os.environ.get("WORLD_DIRECTOR_CLI_COMMAND")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            response_path = root / "valid.json"
            response_path.write_text(json.dumps(valid), encoding="utf-8")
            script = root / "stdout_provider.py"
            script.write_text(
                "from pathlib import Path\nimport sys\nprint(Path(sys.argv[1]).read_text())\n",
                encoding="utf-8",
            )
            os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = (
                f"{sys.executable} {script} {response_path}"
            )
            try:
                response = COMPANION.cli_response(request, root / "interpret-response.json")
                self.assertEqual(response["stage"], "interpret")
                self.assertEqual(response["diagnostics"]["providerAttemptCount"], 1)
            finally:
                if previous is None:
                    os.environ.pop("WORLD_DIRECTOR_CLI_COMMAND", None)
                else:
                    os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = previous

    def test_population_over_maximum_is_rejected(self) -> None:
        request = integrated_request_for("population")
        response = COMPANION.fixture_response(request)
        response["payload"]["residents"].extend(
            json.loads(json.dumps(response["payload"]["residents"][:7]))
        )
        with self.assertRaisesRegex(ValueError, "too many items|unique"):
            COMPANION.validate_stage_response(request, response)

    def test_local_population_synthesis_is_bounded_and_semantically_valid(self) -> None:
        request = integrated_request_for("population")
        response = COMPANION.synthesized_population_response(request)
        self.assertEqual(len(response["payload"]["residents"]), 20)
        self.assertEqual(response["diagnostics"]["inputTokens"], 0)
        self.assertLess(len(json.dumps(response).encode("utf-8")), COMPANION.MAX_RESPONSE_BYTES)
        COMPANION.validate_population_semantics(request, response)

        payload = response["payload"]
        secret_fact_ids = {fact["id"] for fact in request["current"]["facts"] if fact["bSecret"]}
        covered_fact_ids = {belief["factId"] for belief in payload["beliefs"]}
        self.assertLessEqual(secret_fact_ids, covered_fact_ids)
        self.assertGreater(len({resident["displayName"] for resident in payload["residents"]}), 1)
        self.assertGreater(len({resident["occupationTag"] for resident in payload["residents"]}), 1)
        self.assertGreater(len({resident["motivation"] for resident in payload["residents"]}), 1)
        self.assertGreater(len(covered_fact_ids), 1)

    def test_population_bundle_repair_replaces_every_coupled_section(self) -> None:
        request = integrated_request_for("repair")
        request["populationBundleRepair"] = True
        request["repairTargets"] = [
            {"section": section} for section in COMPANION.STAGE_KEYS["population"]
        ]
        response = COMPANION.synthesized_population_repair_response(request)
        self.assertEqual(
            {item["section"] for item in response["payload"]["replacements"]},
            set(COMPANION.STAGE_KEYS["population"]),
        )
        self.assertLess(len(json.dumps(response).encode("utf-8")), COMPANION.MAX_RESPONSE_BYTES)

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
