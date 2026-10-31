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

The model is concentrated where it creates visible value: place identity, landform, history, tension, and town topology. Unreal owns terrain, exact placement, assets, collision, navigation, validation, and actual outcomes. Population, relationships, beliefs, schedules, and the change project are derived locally from the accepted topology, so they stay off the model-critical path but still reflect each world's premise.

## Current functionality

- Player-facing **Create World** flow; the model list is read from the installed Codex CLI's catalog, plus Claude models (`claude-opus`, `claude-sonnet`, `claude-fable`, `claude-haiku`) through the Claude Code CLI when it is installed.
- Asynchronous local CLI companion with cancellation, a total run budget, per-stage timeouts, and teardown-safe callbacks.
- Two creative model stages (interpretation and topology) whose output is constrained by the provider's structured output (`--output-schema`), followed by deterministic layout selection, topology-derived population synthesis, integration, bounded repair, runtime compilation, and a recorded playability check.
- Seeded V3 physical generation across a 1.2 km domain: composable macroforms, thermal erosion, connected water, terrain-aware districts and plots, graded routes, coherent farms, and layered biome dressing.
- One continuous runtime terrain mesh with smooth normals, collision, and a project-owned four-layer grass/gravel/farm/rock material driven by deterministic vertex-color masks.
- Seed-specific settlement morphologies, landmark approaches, terrain affinities, and environmental-story clusters that make physical layout reflect the generated world premise.
- A topology-derived population: named authorities and households, occupation from workplace, occupation-specific schedules (early farmers, late merchants, night watch), family/coworker/authority/rival ties, history events, and dialogue that reflects what residents have learned at runtime and opens up as the player helps them.
- A recurring central threat: on alternating evenings an alarm sends the affected households to the shelter or landmark, which shows in the event log, dialogue, and relationships.
- Deterministic simulation that continues without a connected model.
- Location-repurposing projects validated and executed by Unreal once runtime navigation is ready; temporarily blocked projects retry instead of stalling.
- Provider-neutral stage envelopes with bounded requests/responses, strict retry normalization, cross-platform process-tree cancellation, stdout-or-file response transport, and portable JSON artifacts suitable for a future sandboxed agent SDK.
- AI diagnostics for requests, prompts, per-attempt raw responses, provider events, token/cost telemetry, normalization, validation repairs, physical recipes, and candidate fingerprints.
- Director inspection for world facts, relationships, ownership, simulation state, and project consequences.
- A buildable manifest in the prompt and closed `brief.presentation` choices the model makes, each built from the installed village pack: fortification (fence ring, timber palisade with gates, or stockade with watchtowers and spiked barricades), water life (piers, moored boats, bridges), justice (gallows, guillotine, coffins), fire (lit braziers, street vigils with watchfires, smoke over threatened buildings), arrival time, weather (fog, light, colour grading, wind), season foliage tint, and roof colours for the town and its seat of authority.
- A per-run expression report: every model-authored value is marked `Applied`, `Defaulted`, `NotApplicable`, or `TextOnly`, so prose that changes nothing in the world is visible.

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
- macOS/Xcode for the currently verified build path. Xcode 27 ships without the Metal Toolchain; install it once with `xcodebuild -downloadComponent MetalToolchain` or shader compilation and rendered captures fail with "missing Metal Toolchain"
- Git LFS for `.uasset` and `.umap` files
- A locally authenticated Codex CLI with `exec --output-schema` (verified with 0.166.0) only when intentionally testing real AI generation. Update it with `npm install -g @openai/codex@latest`; the game reads available models from `~/.codex/models_cache.json`, and `[WorldDirector] DefaultModel` in `Config/DefaultGame.ini` picks the default
- Or the Claude Code CLI (the engine behind the Claude Agent SDK), signed in with `claude auth login` or with `ANTHROPIC_API_KEY` set, to generate with Claude instead. Pick a `claude-` model in Create World, or set `DefaultModel=claude-opus`. See `Docs/WorldDirector/StagedGeneration.md`

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
| `L` | Open the saved-world browser |
| `N` | Open or close Create World |
| `F8` | Open or close AI Diagnostics |
| `Tab` | Toggle the interaction cursor |
| `I` | Open or close World Director inspection |
| `M` | Open or close the top-down settlement map |

Within Create World, `Ctrl+Enter` starts generation. In Diagnostics, `Ctrl+Left` and `Ctrl+Right` change the selected stage and `Ctrl+C` copies the visible report.

On the landing screen, choose a completed world and select `LOAD SELECTED WORLD` to rebuild it from its local replay recipe.

The settlement map (`M`) is a straight-down render of the town as built: real roofs, roads, walls and fields, with north (world +X) up. Building names, district rings and roads from the plan are drawn over it, and a side panel lists the layout: landform, districts, building count, roads, perimeter and waterfront. It opens framed on the town. Drag to pan, scroll or press `+`/`-` to zoom, `F` to frame the town again, `T` to show the whole terrain (piers can sit a few hundred metres out on the nearest shore), `V` to switch between the top-down render and the plain schematic, and `L` to hide or show building names. Hover a marker for details, click to set or move a waypoint, and right-click to clear it. The line on your marker shows which way you are facing.

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

Re-validate the saved real model outputs in
`Resources/Fixtures/model-output-corpus.json` (no model calls), or measure live
semantic reliability without Unreal (each prompt costs at least two model calls):

```sh
python3 WorldGen/Scripts/worldgen_batch.py --replay-corpus
python3 WorldGen/Scripts/worldgen_batch.py --live --prompt "A salt-marsh ferry town" --report /tmp/batch.json
```

The project-owned terrain and civic-paving materials are versioned as
`/Game/WorldDirector/Materials/M_WorldDirectorTerrainBlend` and
`/Game/WorldDirector/Materials/M_WorldDirectorPaving`. Their graphs can be
rebuilt from the already-installed certified textures by running
`WorldGen/Scripts/build_world_director_terrain_material.py` through
`UnrealEditor-Cmd`; the script does not download or generate artwork.

For deterministic visual review, add `-WorldDirectorVisualCapture` and one of
`-WorldDirectorVisualView=overview|approach|landmark|civic|topdown|eye|street|district|overlook|map`
when launching `L_WorldDirectorTown`. Add `-WorldDirectorVisualOutput=/absolute/path.png`
to choose the screenshot destination. This path always uses the local fixture and
does not invoke the configured model. Captures are taken at noon unless the world's
`timeOfDay` is set to something other than `dawn`, in which case they show that time.

`-RenderOffScreen` is **required** on macOS. Without it the Metal backend writes a
well-formed but completely black PNG (an `AGX ... Region height OOB` assertion
appears on stderr). The harness now decodes the frame it wrote and fails with
`reason=blank_frame` rather than reporting success for an empty image, so a missing
`-RenderOffScreen` shows up as a capture failure instead of a passing black frame.

`map` opens the settlement map and keeps the UI in the screenshot. The first five views are elevated (3.6 m to 300 m up). `eye`, `street`, `district`
and `overlook` line-trace the collision surface and stand the camera 170 cm above
it, which is the only way ground-level terrain and composition defects are visible.

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nop4 -nosplash -nomcp -RenderOffScreen -ResX=1600 -ResY=900 \
  -WorldDirectorVisualCapture -WorldDirectorVisualView=street \
  -WorldDirectorVisualOutput=/absolute/path.png
```

### Testing presentation choices without a model

`-WorldDirectorPresentationOverride=field=value,...` replaces any of the nine
`brief.presentation` fields for that launch, in fixture and newly generated
worlds (a saved world keeps the presentation it was saved with); unknown fields or
values are ignored with a warning.
`-WorldDirectorTerrainOverride=coast` (or `basin`, `valley`, `ridge`, `marsh`)
sets the landform, which water life needs. For example, walk a misty autumn
stockade town at dusk:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown -game \
  -WorldDirectorPresentationOverride=fortification=stockade,justice=grim,fire=vigil,timeOfDay=dusk,weather=misty,season=autumn,roofPalette=slate,authorityColor=crimson
```

| Field | Values (first is the default) |
|---|---|
| `fortification` | `open`, `fenced`, `palisade`, `stockade` |
| `waterLife` | `none`, `fishing`, `harbor` (coast, valley, or marsh only) |
| `justice` | `lenient`, `stern`, `grim` |
| `fire` | `hearths`, `braziers`, `vigil`, `smouldering` (needs a threat with affected locations) |
| `timeOfDay` | `dawn`, `morning`, `midday`, `dusk`, `night` |
| `weather` | `clear`, `overcast`, `misty`, `stormy` |
| `season` | `summer`, `spring`, `autumn` |
| `roofPalette` | `natural`, `weathered`, `sooty`, `ochre`, `slate`, `moss` |
| `authorityColor` | `natural`, `crimson`, `azure`, `gold`, `verdant`, `black` |

Each resolve logs `WORLD_DIRECTOR_PRESENTATION` with what was placed, and a
generation run logs `WORLD_DIRECTOR_EXPRESSION` and writes the same report to the
`expression` block of `Saved/WorldRuns/<run-id>/run-summary.json`.

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
