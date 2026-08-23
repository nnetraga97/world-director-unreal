# World Director

World Director is an Unreal Engine 5.8 vertical slice for generating and simulating a living stylized town. A player describes a world, a staged AI director produces semantic world data, Unreal validates that data, and a deterministic runtime compiler builds the physical town exclusively from certified authored assets.

The core boundary is deliberate:

```text
Player prompt
  -> staged AI semantic specification
  -> strict schema and continuity validation
  -> seeded physical world recipe
  -> certified Unreal assets
  -> runtime town and simulation
```

The model determines meaning—identity, history, population, relationships, beliefs, tensions, and proposed changes. Unreal owns terrain, exact placement, assets, collision, navigation, schedules, validation, and actual outcomes.

## Current functionality

- Player-facing **Create World** flow with model and reasoning-effort selection.
- Asynchronous local CLI companion with cancellation, per-stage timeouts, and teardown-safe callbacks.
- Staged interpretation, topology, layout selection, population, integration, repair, and runtime compilation.
- Seeded V3 physical generation across a 1.2 km domain: composable macroforms, thermal erosion, connected water, terrain-aware districts and plots, graded routes, coherent farms, and layered biome dressing.
- One continuous runtime terrain mesh with smooth normals, collision, and a project-owned four-layer grass/gravel/farm/rock material driven by deterministic vertex-color masks.
- Seed-specific settlement morphologies, landmark approaches, terrain affinities, and environmental-story clusters that make physical layout reflect the generated world premise.
- Town population with homes, occupations, schedules, relationships, beliefs, memories, and lightweight dialogue.
- Deterministic simulation that continues without a connected model.
- Location-repurposing projects validated and executed by Unreal.
- AI diagnostics for requests, prompts, raw responses, provider events, token/cost telemetry, validation repairs, physical recipes, and candidate fingerprints.
- Director inspection for world facts, relationships, ownership, simulation state, and project consequences.

## Repository layout

```text
WorldGen/                                      Unreal project
WorldGen/Plugins/WorldDirector/                Runtime, editor, and test modules
WorldGen/Plugins/WorldDirector/Resources/      Schemas, fixtures, and CLI companion
WorldGen/Content/CapabilityPack/               Game-owned certified wrappers
WorldGen/Content/WorldDirector/                Maps, material, StateTree, PCG, and profiles
WorldGen/Scripts/                              Model-free bridge tests and editor asset tooling
Docs/WorldDirector/                            Architecture and subsystem notes
initial_plan.md                                Original vertical-slice execution plan
```

## Requirements

- Unreal Engine 5.8
- macOS/Xcode for the currently verified build path
- Git LFS for `.uasset` and `.umap` files
- A locally authenticated Codex-compatible CLI only when intentionally testing real AI generation

The repository intentionally excludes locally installed Fab/marketplace packs. The active `Profile.StylizedVillage` references the installed `Fantastic_Village_Pack`, while other downloaded construction, cabin, western, and science-fiction environments remain optional local experiments and are not mixed into that profile. Restore required marketplace content through the account that owns its license.

## Setup

```sh
git lfs install
git clone https://github.com/nnetraga97/world-director-unreal.git
cd world-director-unreal
git lfs pull
```

Restore the required licensed content into `WorldGen/Content/`, then open:

```text
WorldGen/WorldGen.uproject
```

The primary test map is:

```text
/Game/WorldDirector/Maps/L_WorldDirectorTown
```

## Controls

| Key | Action |
| --- | --- |
| `N` | Open or close Create World |
| `F8` | Open or close AI Diagnostics |
| `Tab` | Toggle the interaction cursor |
| `I` | Open or close World Director inspection |

Within Create World, `Ctrl+Enter` starts generation. In Diagnostics, `Ctrl+Left` and `Ctrl+Right` change the selected stage and `Ctrl+C` copies the visible report.

Click a resident while the interaction cursor is enabled to open dialogue.

## Safe local verification

Build the editor target on macOS:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" \
  WorldGenEditor Mac Development "$PWD/WorldGen/WorldGen.uproject" -WaitMutex
```

Run the deterministic fixture and transient-world automation suite:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/WorldGen/WorldGen.uproject" \
  -unattended -nop4 -nosplash -nullrhi -nomcp \
  -ExecCmds="Automation RunTests WorldDirector.;Quit" \
  -TestExit="Automation Test Queue Empty"
```

These automation tests use local fixtures and do **not** invoke an AI model. In PIE, enable **Debug fixture (no model)** in Create World for the same model-free player-flow test.

Run the companion prompt/response contract tests without Unreal or a provider:

```sh
python3 -m unittest discover -s WorldGen/Scripts -p 'test_*.py' -v
```

The project-owned terrain and civic-paving materials are versioned as
`/Game/WorldDirector/Materials/M_WorldDirectorTerrainBlend` and
`/Game/WorldDirector/Materials/M_WorldDirectorPaving`. Their graphs can be
rebuilt from the already-installed certified textures by running
`WorldGen/Scripts/build_world_director_terrain_material.py` through
`UnrealEditor-Cmd`; the script does not download or generate artwork.

For deterministic visual review, add `-WorldDirectorVisualCapture` and one of
`-WorldDirectorVisualView=overview|approach|landmark|civic|topdown|eye|street|district|overlook`
when launching `L_WorldDirectorTown`. Add `-WorldDirectorVisualOutput=/absolute/path.png`
to choose the screenshot destination. This path always uses the local fixture and
does not invoke the configured model.

`-RenderOffScreen` is **required** on macOS. Without it the Metal backend writes a
well-formed but completely black PNG (an `AGX ... Region height OOB` assertion
appears on stderr). The harness now decodes the frame it wrote and fails with
`reason=blank_frame` rather than reporting success for an empty image, so a missing
`-RenderOffScreen` shows up as a capture failure instead of a passing black frame.

The first five views are elevated (3.6 m to 300 m up). `eye`, `street`, `district`
and `overlook` line-trace the collision surface and stand the camera 170 cm above
it, which is the only way ground-level terrain and composition defects are visible.

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nop4 -nosplash -nomcp -RenderOffScreen -ResX=1600 -ResY=900 \
  -WorldDirectorVisualCapture -WorldDirectorVisualView=street \
  -WorldDirectorVisualOutput=/absolute/path.png
```

Real model generation is never required for build or fixture verification. Configure and invoke it only deliberately; generated diagnostics are written beneath `WorldGen/Saved/WorldRuns/` and remain local.

## Architecture documentation

- [World generation foundation](Docs/WorldDirector/WorldGenerationV2.md)
- [World compiler](Docs/WorldDirector/WorldCompiler.md)
- [Staged generation](Docs/WorldDirector/StagedGeneration.md)
- [Player world creation](Docs/WorldDirector/PlayerWorldCreation.md)
- [Living-town simulation](Docs/WorldDirector/LivingTownSimulation.md)
- [Dynamic world edits](Docs/WorldDirector/DynamicWorldEdits.md)
- [Inspection and hardening](Docs/WorldDirector/InspectionAndHardening.md)

## Asset and licensing note

Project code and game-owned wrappers are versioned here. Third-party assets retain their original licenses. Fab/marketplace source content is excluded from Git to avoid redistributing licensed packages. No general project license has been granted by this repository.
