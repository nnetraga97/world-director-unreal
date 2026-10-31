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
from unittest import mock


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
CORPUS_PATH = FIXTURE_PATH.with_name("model-output-corpus.json")
SCHEMA_PATH = FIXTURE_PATH.parents[1] / "Schemas" / "world-director.schema.json"


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
        # Interpret and topology carry the buildable manifest (about 2,300 characters).
        budgets = {
            "interpret": 8_000,
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

    def test_provider_schema_is_strict_and_closed(self) -> None:
        def walk(node: object, path: str) -> None:
            if isinstance(node, list):
                for index, item in enumerate(node):
                    walk(item, f"{path}[{index}]")
                return
            if not isinstance(node, dict):
                return
            for keyword in COMPANION.PROVIDER_SCHEMA_DROPPED_KEYWORDS | {"oneOf"}:
                self.assertNotIn(keyword, node, path)
            if node.get("type") == "object" and "properties" in node:
                self.assertIs(node.get("additionalProperties"), False, path)
                self.assertEqual(sorted(node["required"]), sorted(node["properties"]), path)
            for key, child in node.items():
                walk(child, f"{path}.{key}")

        for stage in ("interpret", "topology", "repair"):
            with self.subTest(stage=stage):
                request = integrated_request_for(stage)
                request["repairTargets"] = [{"section": "locations", "index": 0}]
                walk(COMPANION.provider_output_schema(COMPANION.stage_response_schema(request)), "$")

    def test_prompt_omits_schema_when_provider_enforces_it(self) -> None:
        request = integrated_request_for("topology")
        with_schema = COMPANION.build_agent_prompt(request)
        without_schema = COMPANION.build_agent_prompt(request, include_schema=False)
        self.assertIn("Stage-specific JSON Schema", with_schema)
        self.assertNotIn("Stage-specific JSON Schema", without_schema)
        self.assertLess(len(without_schema), len(with_schema) - 3000)
        self.assertIn("brief.landform", COMPANION.build_agent_prompt(request_for("interpret")))

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
        # A corrective retry needs at least MINIMUM_RETRY_SECONDS of budget left.
        request["companionTimeoutSeconds"] = 60
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
        payload = response["payload"]
        self.assertGreaterEqual(len(payload["residents"]), 20)
        self.assertLessEqual(len(payload["residents"]), 30)
        self.assertEqual(response["diagnostics"]["inputTokens"], 0)
        COMPANION.validate_population_semantics(request, response)

        secret_fact_ids = {fact["id"] for fact in request["current"]["facts"] if fact["bSecret"]}
        covered_fact_ids = {belief["factId"] for belief in payload["beliefs"]}
        self.assertLessEqual(secret_fact_ids, covered_fact_ids)
        self.assertEqual(
            len({resident["displayName"] for resident in payload["residents"]}),
            len(payload["residents"]),
        )
        occupations = [resident["occupationTag"] for resident in payload["residents"]]
        self.assertEqual(occupations.count("Occupation.Reeve"), 1)
        self.assertGreaterEqual(len(set(occupations)), 3)
        self.assertGreaterEqual(
            len({item["relationshipType"] for item in payload["relationships"]}), 3
        )
        self.assertTrue(payload["events"])

    def test_population_synthesis_follows_each_real_topology(self) -> None:
        corpus = json.loads(CORPUS_PATH.read_text(encoding="utf-8"))["entries"]
        signatures = set()
        for entry in corpus:
            with self.subTest(run=entry["runId"]):
                topology_request = integrated_request_for("topology")
                topology_request["seed"] = entry["seed"]
                topology_request["current"]["brief"] = entry["brief"]
                topology = {"stage": "topology", "payload": json.loads(json.dumps(entry["topology"]))}
                topology, _ = COMPANION.normalize_and_validate_stage_response(topology_request, topology)
                request = integrated_request_for("population")
                request["seed"] = entry["seed"]
                request["current"].update({
                    "brief": entry["brief"],
                    **{key: topology["payload"][key] for key in ("topology", "locations", "facts", "threats")},
                })
                response = COMPANION.synthesized_population_response(request)
                COMPANION.validate_population_semantics(request, response)
                payload = response["payload"]
                self.assertLess(len(json.dumps(response, separators=(",", ":"))), 256_000)
                names = [resident["displayName"] for resident in payload["residents"]]
                self.assertFalse(any(
                    word in name.lower()
                    for name in names
                    for word in ("generated", "authority", "council", "resident")
                ))
                signatures.add(tuple(sorted(names)))
                motivations = {resident["motivation"] for resident in payload["residents"]}
                self.assertGreater(len(motivations), len(payload["residents"]) // 2)
        self.assertEqual(len(signatures), len(corpus))

    def test_topology_requires_the_full_slice_location_mix(self) -> None:
        request = integrated_request_for("topology")
        response = COMPANION.fixture_response(request)
        for location in response["payload"]["locations"]:
            if location["purposeTag"] == "Purpose.Landmark":
                location["purposeTag"] = "Purpose.Workplace"
        with self.assertRaisesRegex(ValueError, "Purpose.Landmark"):
            COMPANION.normalize_and_validate_stage_response(request, response)

        response = COMPANION.fixture_response(request)
        landmark = next(
            item["id"] for item in response["payload"]["locations"]
            if item["purposeTag"] == "Purpose.Landmark"
        )
        response["payload"]["topology"]["landmarkLocationIds"] = ["location.missing"]
        for location in response["payload"]["locations"]:
            location["accessPolicy"] = "Residents"
        response, changes = COMPANION.normalize_and_validate_stage_response(request, response)
        self.assertEqual(response["payload"]["topology"]["landmarkLocationIds"][0], landmark)
        self.assertTrue(any("public" in change for change in changes))

    def test_interpret_requires_a_supported_landform(self) -> None:
        request = request_for("interpret")
        response = COMPANION.fixture_response(request)
        self.assertIn(response["payload"]["brief"]["landform"], COMPANION.LANDFORMS)
        response["payload"]["brief"]["landform"] = "volcano"
        with self.assertRaisesRegex(ValueError, "not one of the allowed choices|landform"):
            COMPANION.normalize_and_validate_stage_response(request, response)

    def test_interpret_requires_a_supported_presentation(self) -> None:
        request = request_for("interpret")
        response = COMPANION.fixture_response(request)
        self.assertEqual(response["payload"]["brief"]["presentation"], COMPANION.PRESENTATION_DEFAULTS)
        response["payload"]["brief"]["presentation"]["fortification"] = "castle"
        with self.assertRaisesRegex(ValueError, "not one of the allowed choices|fortification"):
            COMPANION.normalize_and_validate_stage_response(request, response)

    def test_briefs_without_presentation_get_the_defaults(self) -> None:
        request = request_for("interpret")
        response = COMPANION.fixture_response(request)
        del response["payload"]["brief"]["presentation"]
        normalized, changes = COMPANION.normalize_and_validate_stage_response(request, response)
        self.assertEqual(normalized["payload"]["brief"]["presentation"], COMPANION.PRESENTATION_DEFAULTS)
        self.assertTrue(any("presentation" in change for change in changes))

    def test_presentation_options_match_the_schema(self) -> None:
        schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        properties = schema["$defs"]["WorldPresentation"]["properties"]
        self.assertEqual(sorted(properties), sorted(COMPANION.PRESENTATION_OPTIONS))
        for name, options in COMPANION.PRESENTATION_OPTIONS.items():
            self.assertEqual(properties[name]["enum"], list(options), name)

    def test_fallback_manifest_offers_exactly_the_schema_options(self) -> None:
        manifest = COMPANION.DEFAULT_BUILDABLE_MANIFEST["presentation"]
        self.assertEqual(sorted(manifest), sorted(COMPANION.PRESENTATION_OPTIONS))
        for name, options in COMPANION.PRESENTATION_OPTIONS.items():
            self.assertEqual(list(manifest[name]), list(options), name)
        prompt = COMPANION.build_agent_prompt(request_for("interpret"), include_schema=False)
        self.assertIn("stockade", prompt)
        self.assertIn("notAvailable", prompt)

    def test_buildable_manifest_reaches_only_the_stages_that_build(self) -> None:
        buildable = {
            "presentation": {"fortification": {"stockade": "PRESENTATION_MARKER"}},
            "buildings": [{"building": "Watermill", "when": "BUILDINGS_MARKER"}],
            "notAvailable": "NOT_AVAILABLE_MARKER",
        }
        expected = {
            "interpret": {"PRESENTATION_MARKER", "NOT_AVAILABLE_MARKER"},
            "topology": {"BUILDINGS_MARKER", "NOT_AVAILABLE_MARKER"},
            "repair": set(),
            "population": set(),
        }
        markers = {"PRESENTATION_MARKER", "BUILDINGS_MARKER", "NOT_AVAILABLE_MARKER"}
        for stage, present in expected.items():
            with self.subTest(stage=stage):
                request = integrated_request_for(stage)
                request["capabilitySummary"]["buildable"] = buildable
                prompt = COMPANION.build_agent_prompt(request, include_schema=False)
                for marker in markers:
                    if marker in present:
                        self.assertIn(marker, prompt)
                    else:
                        self.assertNotIn(marker, prompt)

    def test_model_choice_is_checked_against_the_codex_catalog(self) -> None:
        previous = os.environ.get("CODEX_HOME")
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "models_cache.json").write_text(json.dumps({"models": [
                {"slug": "model-current", "supported_reasoning_levels": [
                    {"effort": "low"}, {"effort": "medium"}]},
            ]}), encoding="utf-8")
            os.environ["CODEX_HOME"] = directory
            try:
                COMPANION.validate_model_choice("model-current", "medium")
                with self.assertRaisesRegex(ValueError, "not in the installed Codex model catalog"):
                    COMPANION.validate_model_choice("model-retired", "medium")
                with self.assertRaisesRegex(ValueError, "does not support reasoning effort"):
                    COMPANION.validate_model_choice("model-current", "max")
            finally:
                if previous is None:
                    os.environ.pop("CODEX_HOME", None)
                else:
                    os.environ["CODEX_HOME"] = previous

    def test_configured_provider_receives_the_output_schema(self) -> None:
        request = request_for("interpret")
        request["providerMode"] = "cli"
        request["companionTimeoutSeconds"] = 10
        valid = COMPANION.fixture_response(request)
        previous = os.environ.get("WORLD_DIRECTOR_CLI_COMMAND")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            response_path = root / "valid.json"
            response_path.write_text(json.dumps(valid), encoding="utf-8")
            schema_copy = root / "schema-seen.json"
            script = root / "schema_provider.py"
            script.write_text(
                "from pathlib import Path\nimport shutil, sys\n"
                "shutil.copy(sys.argv[1], sys.argv[2])\n"
                "Path(sys.argv[4]).write_text(Path(sys.argv[3]).read_text())\n",
                encoding="utf-8",
            )
            os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = (
                f"{sys.executable} {script} {{schema}} {schema_copy} {response_path} {{output}}"
            )
            try:
                response = COMPANION.cli_response(request, root / "interpret-response.json")
                self.assertTrue(response["diagnostics"]["structuredOutput"])
                schema = json.loads(schema_copy.read_text(encoding="utf-8"))
                self.assertEqual(schema["properties"]["stage"]["const"], "interpret")
                prompt = (root / "interpret-prompt.txt").read_text(encoding="utf-8")
                self.assertNotIn("Stage-specific JSON Schema", prompt)
            finally:
                if previous is None:
                    os.environ.pop("WORLD_DIRECTOR_CLI_COMMAND", None)
                else:
                    os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = previous

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

    def test_corrective_retry_is_skipped_when_the_budget_cannot_fit_it(self) -> None:
        request = request_for("interpret")
        request["providerMode"] = "cli"
        request["providerMaxAttempts"] = 2
        request["companionTimeoutSeconds"] = 10
        previous = os.environ.get("WORLD_DIRECTOR_CLI_COMMAND")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "bad_provider.py"
            counter = root / "count.txt"
            script.write_text(
                "from pathlib import Path\nimport sys\n"
                "c = Path(sys.argv[1]); c.write_text(str(int(c.read_text() or 0) + 1) if c.exists() else '1')\n"
                "Path(sys.argv[2]).write_text('{\"stage\":\"interpret\",\"stage\":\"repair\"}')\n",
                encoding="utf-8",
            )
            os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = f"{sys.executable} {script} {counter} {{output}}"
            try:
                with self.assertRaisesRegex(RuntimeError, "not enough for a corrective retry"):
                    COMPANION.cli_response(request, root / "interpret-response.json")
                self.assertEqual(counter.read_text(encoding="utf-8"), "1")
            finally:
                if previous is None:
                    os.environ.pop("WORLD_DIRECTOR_CLI_COMMAND", None)
                else:
                    os.environ["WORLD_DIRECTOR_CLI_COMMAND"] = previous

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

    def _run_fake_claude(
        self, directory: Path, request: dict, result: dict, exit_code: int = 0
    ) -> tuple[dict, dict]:
        """Run cli_response against a stand-in `claude` that records how it was called."""
        record_path = directory / "record.json"
        result_path = directory / "result.json"
        result_path.write_text(json.dumps(result), encoding="utf-8")
        script = directory / "claude"
        script.write_text(
            f"#!{sys.executable}\n"
            + textwrap.dedent(
                """
                import json, os, sys
                from pathlib import Path
                prompt = sys.stdin.read()
                Path(os.environ["FAKE_CLAUDE_RECORD"]).write_text(json.dumps({
                    "argv": sys.argv[1:], "cwd": os.getcwd(),
                    "cwdEntries": os.listdir("."), "prompt": prompt}))
                print(Path(os.environ["FAKE_CLAUDE_RESULT"]).read_text())
                sys.exit(int(os.environ["FAKE_CLAUDE_EXIT"]))
                """
            ),
            encoding="utf-8",
        )
        script.chmod(0o755)
        environment = {
            "WORLD_DIRECTOR_CLAUDE_PATH": str(script),
            "FAKE_CLAUDE_RECORD": str(record_path),
            "FAKE_CLAUDE_RESULT": str(result_path),
            "FAKE_CLAUDE_EXIT": str(exit_code),
        }
        with mock.patch.dict(os.environ, environment):
            os.environ.pop("WORLD_DIRECTOR_CLI_COMMAND", None)
            response = COMPANION.cli_response(request, directory / "interpret-response.json")
        return response, json.loads(record_path.read_text(encoding="utf-8"))

    def test_claude_models_run_the_claude_cli_with_the_stage_schema(self) -> None:
        self.assertEqual(COMPANION.model_provider("claude-sonnet"), "claude")
        self.assertEqual(COMPANION.model_provider("gpt-6.1-sol"), "codex")
        request = request_for("interpret")
        request["providerMode"] = "cli"
        request["model"] = "claude-sonnet"
        request["reasoningEffort"] = "medium"
        request["companionTimeoutSeconds"] = 10
        result = {
            "type": "result",
            "subtype": "success",
            "is_error": False,
            "session_id": "session-1",
            "result": "",
            "structured_output": COMPANION.fixture_response(request),
            "total_cost_usd": 0.0123,
            "usage": {
                "input_tokens": 100,
                "cache_creation_input_tokens": 20,
                "cache_read_input_tokens": 300,
                "output_tokens": 50,
                "output_tokens_details": {"thinking_tokens": 10},
            },
            "modelUsage": {"claude-sonnet-5-5": {}},
        }
        with tempfile.TemporaryDirectory() as directory:
            response, record = self._run_fake_claude(Path(directory), request, result)
        self.assertEqual(response["stage"], "interpret")
        argv = record["argv"]
        self.assertIn("-p", argv)
        self.assertEqual(argv[argv.index("--model") + 1], "sonnet")
        self.assertEqual(argv[argv.index("--effort") + 1], "medium")
        self.assertEqual(argv[argv.index("--tools") + 1], "")
        self.assertEqual(argv[argv.index("--output-format") + 1], "json")
        for isolation_flag in ("--safe-mode", "--strict-mcp-config", "--no-session-persistence"):
            self.assertIn(isolation_flag, argv)
        schema = json.loads(argv[argv.index("--json-schema") + 1])
        self.assertEqual(schema["properties"]["stage"]["const"], "interpret")
        self.assertEqual(record["cwdEntries"], [])
        self.assertNotEqual(Path(record["cwd"]).resolve(), Path.cwd().resolve())
        self.assertNotIn("Stage-specific JSON Schema", record["prompt"])
        diagnostics = response["diagnostics"]
        self.assertEqual(diagnostics["provider"], "claude")
        self.assertEqual(diagnostics["threadId"], "session-1")
        self.assertEqual(diagnostics["inputTokens"], 420)
        self.assertEqual(diagnostics["cachedInputTokens"], 300)
        self.assertEqual(diagnostics["outputTokens"], 50)
        self.assertEqual(diagnostics["reasoningOutputTokens"], 10)
        self.assertEqual(diagnostics["resolvedModel"], "claude-sonnet-5-5")
        self.assertAlmostEqual(diagnostics["billedCostUsd"], 0.0123)

    def test_full_claude_model_names_reach_the_cli_unchanged(self) -> None:
        request = request_for("interpret")
        request["providerMode"] = "cli"
        request["model"] = "claude-opus-5-5"
        request["companionTimeoutSeconds"] = 10
        result = {"type": "result", "subtype": "success", "is_error": False,
                  "structured_output": COMPANION.fixture_response(request)}
        with tempfile.TemporaryDirectory() as directory:
            response, record = self._run_fake_claude(Path(directory), request, result)
        argv = record["argv"]
        self.assertEqual(argv[argv.index("--model") + 1], "claude-opus-5-5")
        self.assertNotIn("--effort", argv)
        self.assertIsNone(response["diagnostics"]["billedCostUsd"])

    def test_claude_cli_failure_reports_its_own_message(self) -> None:
        request = request_for("interpret")
        request["providerMode"] = "cli"
        request["model"] = "claude-opus"
        request["providerMaxAttempts"] = 1
        request["companionTimeoutSeconds"] = 10
        # The CLI reports a failed sign-in with subtype "success" and is_error true.
        result = {"type": "result", "subtype": "success", "is_error": True,
                  "result": "Failed to authenticate: OAuth session expired", "total_cost_usd": 0}
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, "Failed to authenticate"):
                self._run_fake_claude(Path(directory), request, result, exit_code=1)
            telemetry = json.loads(
                Path(directory, "interpret-telemetry.json").read_text(encoding="utf-8")
            )
        self.assertEqual(telemetry["companionOutcome"], "provider_error")
        self.assertEqual(telemetry["provider"], "claude")


if __name__ == "__main__":
    unittest.main()
