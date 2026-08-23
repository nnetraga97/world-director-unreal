# World Director Town Compiler

Phase 3 compiles the hand-authored `compiler-town.json` fixture through the
same runtime path future director output will use:

```text
GeneratedWorldSpec -> validation -> semantic layout -> exact asset resolution
                   -> ResolvedWorldPlan -> runtime spawn -> viability gate
```

The fixture contains one terrain envelope, eight locations, eight residents,
homes, workplaces, a landmark, a corridor-first tree-and-spur route network, enterable interiors,
working doors, and basic daily schedule intents. `L_WorldDirectorTown` contains
only the terrain/lighting envelope, one generously sized navigation volume, a
player start, the fixture bootstrap, and PCG dressing. The town itself is
created during `BeginPlay`; no manual placement is part of compilation.

## Resolution and placement

`FWorldDirectorCompiler::Resolve` is the only semantic-to-asset boundary. The
AI-facing specification remains free of Unreal paths. The resolved plan stores
the exact terrain, shell, interior, character-part, animation, transform,
footprint, entrance, route, and schedule data needed by runtime spawn.

The V3 physical generator replaces the former 18 fixed slots and quarter-turn
shuffle. It creates a seeded basin, valley, ridge, coast, or marsh heightfield;
solves district anchors and non-overlapping terrain-aware plots; creates a
corridor-first curved route network with one protected landmark approach;
selects only two or three semantic civic courts; classifies textured surfaces;
and places exclusion-aware dressing. Candidate IDs hash the complete physical
result.

The resolved recipe is bit-identical at its explicitly quantized physical-data
boundary for the same seed, intent, generator/content versions, and profile.
Rendering pixels, physics tick ordering, and navmesh internal serialization are
not guaranteed. See `WorldGenerationV2.md` for the exact replay contract.

Runtime door-through navigation narrowed the active shell pool further than
the Phase 1 visual audit. The compiler currently selects:

* `BP_Cap_Home_Compact_01` and `BP_Cap_Home_Multiwing_01` for residences;
* `BP_Cap_Workplace_Longhouse_01` and `BP_Cap_Workplace_Inn_01` for public and
  working locations;
* `BP_Cap_Workplace_Guildhall_01` for the primary landmark.

The tall-compound and broad-compact shells remain installed and useful art
assets, but they have not passed the same runtime entrance gate and remain
unavailable to procedural selection.

## Runtime construction

`AWorldDirectorTownActor` first realizes the generated terrain as a textured,
colliding runtime procedural mesh, then creates non-colliding feathered route,
junction, civic-paving, horizon, and water surfaces plus deterministic instanced
dressing. It spawns the resolved shell/interior pairs, rehosts each Fab door
mesh on an operable `AWorldDirectorDoorActor`, and creates modular Quaternius
residents. Vendor interior wall runs are suppressed because the shell provides
the authoritative outer walls; the floor and compatible furniture remain.

The compiler has four compatible part families per presented body type. It uses
the semantic occupation as a family preference, then the seed and stable resident
ID to select a unique body/head combination. A full 20–30 resident town therefore
uses distinct modular combinations rather than alternating two fixed characters.

Repurposable locations retain their actors/components for later live dressing.
Other static shell and interior components collapse into mesh-keyed instanced
components. Hidden components and vendor doors are never copied during that
collapse. Packed Level Actors are not used.

## PCG boundary

`/Game/WorldDirector/PCG/PCG_TownDressing` remains available as an authored
prototype. V3 dressing is currently compiled into the physical recipe and
spawned deterministically so it can be replayed, fingerprinted, and excluded
from plots, roads, steep terrain, and water. The prototype graph is:

```text
Landscape Data -> Surface Sampler -> Transform Points -> Static Mesh Spawner
```

It sparsely instances non-colliding Fantastic Village grass, hay, barrels,
signposts, and tree trunks within `TownDressing_PCG`. PCG owns visual dressing
only. Locations, routes, relationships, schedules, and narrative state never
enter the graph.

## Viability gate

After dynamic navigation builds, `ValidateNavigationViability` requires:

1. every semantic entrance and resident start projects to navigation;
2. every resident has complete start-to-home, home-to-work, and
   work-to-landmark paths;
3. both sides of every actual vendor-door transform project to distinct nav
   points, and at least one axis has a complete short path through the opening.

The actual-door probe is intentionally separate from semantic routing. It
prevents a plausible facade or a misplaced entrance approximation from hiding
an inaccessible building.

Run the full automation suite from the repository root:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/WorldGen/WorldGen.uproject" -unattended -nop4 -nullrhi -nosplash -nomcp \
  -ExecCmds="Automation RunTests WorldDirector; Quit" \
  -TestExit="Automation Test Queue Empty"
```

Run the physical/navigation gate directly:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nosplash -nullrhi -nomcp -WorldDirectorAutoTest
```

Success is the explicit log marker
`WORLD_DIRECTOR_NAVIGATION_RESULT=PASS locations=18 residents=24` plus one
`WORLD_DIRECTOR_DOOR_PROBE ... result=PASS` line for every location. The
full-slice plot set is qualified at all four quarter-turn rotations through the
test-only `-WorldDirectorCompilerSeedOverride=<seed>` command-line override.

Phase 4 originally kept the eight-location physical plan while scaling the
fixture to 24 residents. Phase 6 expands the same compiler to 18 certified
plots, uses per-home resident spawn slots, and accepts generated towns anywhere
within the 12–18 building contract. See `FullSliceTown.md` for the current
acceptance evidence and `LivingTownSimulation.md` for resident behavior.
