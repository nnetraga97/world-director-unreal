# World Director Canonical Data Model

Phase 2 introduces the project plugin at
`WorldGen/Plugins/WorldDirector/WorldDirector.uplugin` with three modules:

* `WorldDirectorRuntime` owns reflected data structures, strict JSON I/O,
  validation, and the six subsystem boundaries.
* `WorldDirectorEditor` is the future editor-integration boundary. Runtime code
  does not depend on it.
* `WorldDirectorTests` owns developer automation tests and hand-authored JSON
  fixtures.

## Authority boundary

`FGeneratedWorldSpec` is AI-facing and semantic. It contains stable IDs,
purposes, relationships, facts, pressures, and required capability tags. It
must never contain `/Game/` or `/Engine/` asset paths.

`FResolvedWorldPlan` is Unreal-facing. It maps semantic location IDs to exact
terrain, shell, interior, modular character-part, and animation soft paths;
building/entrance transforms and footprints; repurposable flags; route
segments; resident spawn transforms; and basic schedule intents. Phase 3 owns
that resolution step and independently verifies every resolved asset before
runtime spawn.

Names are presentation only. Every cross-reference uses a stable string ID.

Phase 5 makes the generation-time semantic brief explicit: settlement identity
and terrain preferences live on `FWorldBrief`; districts, governance,
historical wound, current tension, supernatural premise, and central threat live
on `FTownTopology`. These remain semantic strings and stable IDs only. The
selected physical layout is an opaque provider-facing candidate ID whose seed
enters `FResolvedWorldPlan`; no candidate exposes Unreal paths to the model.

## Versioning and schemas

Every plan-named structure carries `version: 1`. The canonical Draft 2020-12
schema and per-type entry schemas are in
`WorldGen/Plugins/WorldDirector/Resources/Schemas`. Reflected Unreal booleans
retain their UE wire names (`bEmployed`, `bRepurposable`, and so on), which is
also what strict `FJsonObjectConverter` deserialization requires.

## Runtime subsystems

| Subsystem | Phase 2 responsibility |
|---|---|
| `UCapabilityCatalogSubsystem` | Registers semantic mechanical tags; no asset paths cross into generated specs |
| `UWorldGenerationSubsystem` | Strict JSON load, validation, and lossless known-schema serialization |
| `UWorldStateSubsystem` | Owns the active canonical world state |
| `UTownSimulationSubsystem` | Owns the clock, schedule resolution, claims, relationship events, belief sharing, and offline-safe time skip |
| `UChangeProjectSubsystem` | Validates, schedules, decides, and applies stable-ID repurpose projects to live town state |
| `UDirectorBridgeSubsystem` | Owns the replaceable asynchronous provider, staged run files, timeouts, cancellation, and targeted repair orchestration without becoming simulation authority |

## Validation contract

`FWorldDirectorValidator` emits `FValidationReport` entries with a stable code,
JSON-style path, severity, and useful message. It rejects:

* empty or duplicate stable IDs;
* a resident without a real home;
* an employed resident without a real workplace or registered occupation tag;
* locations without a purpose, owner/controller, access policy, or registered
  required capabilities;
* any missing reference;
* household membership that does not point back consistently;
* occupancy above the home's declared capacity;
* self-referential or broken reciprocal relationships;
* belief confidence outside `[0, 1]`;
* incomplete resident motivation, fear, current-location, memory, or
  availability state;
* an important secret without an established fact;
* a revelation that predates the fact it claims to reveal;
* an Unreal asset path anywhere in the AI-facing generated spec.

## Fixtures and evidence

`valid-world.json` is a hand-authored two-resident town with homes, employment,
a household, reciprocal relationships, an established secret, a past event, a
threat, and a proposed clinic conversion. `invalid-world.json` is valid JSON
but intentionally violates multiple semantic invariants.

The automation filter `WorldDirector.Phase2` proves:

1. the valid fixture loads in strict mode, validates, serializes, reloads, and
   produces identical second-generation JSON;
2. the invalid fixture is rejected with the expected reference, capacity,
   capability, secret, chronology, and asset-boundary codes;
3. corrupting one reciprocal relationship is independently rejected.

Phase 4 adds `living-town.json`, a 24-resident fixture with six households,
six homes, two workplaces, complete resident life-state fields, 24 reciprocal
relationship pairs, 24 beliefs, and six closed facts. Runtime may transfer
beliefs but may never add a fact. See `LivingTownSimulation.md` for the behavior
and physical verification contract.

Run from the project root with:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/WorldGen/WorldGen.uproject" -unattended -nop4 -nullrhi -nosplash \
  -ExecCmds="Automation RunTests WorldDirector.Phase2; Quit" \
  -TestExit="Automation Test Queue Empty"
```
