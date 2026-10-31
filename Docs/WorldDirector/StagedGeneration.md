# World Director Staged Generation

Phase 5 uses a replaceable `IWorldDirectorProvider` implementation backed by a
one-shot local companion process. The companion invokes the configured CLI
agent for one semantic stage at a time; Unreal remains the authority for
capabilities, candidate layout seeds, validation, asset resolution, compilation,
and physical viability.

## Stage contract

1. **Interpret** (model) returns a supported world brief, including
   `brief.landform` — exactly one of `basin`, `valley`, `ridge`, `coast`, or
   `marsh`. Unreal builds terrain from that field alone; briefs saved before the
   field existed fall back to the earliest unnegated landform word in
   `terrainPreferences`. It also returns `brief.presentation`: nine closed
   choices (fortification, water life, justice, fire, time of day, weather,
   season, roof palette, authority colour) picked from the buildable manifest in
   `capabilitySummary.buildable`, which describes the visible effect of every
   option and lists what the pack cannot build. Briefs saved before the field
   existed load with the defaults, which reproduce their original look. A blank
   prompt is permission to invent a setting.
2. **Topology** (model) returns districts, 12–18 locations and their adjacency,
   governance, historical wound, current tension, supernatural premise, central
   threat, facts, and threats. Its prompt carries the manifest's building table:
   the purposes and name words that select the watermill, windmill, civic halls,
   inn, and longhouse. The companion normalizes it (one true
   `Purpose.Landmark` listed first in `landmarkLocationIds`, a public location,
   home capacity, a convertible project target) and rejects it — triggering a
   corrective retry inside the same stage — if it still lacks the location mix
   Unreal's full-slice validation requires.
3. **Layout** is local: Unreal resolves the topology on successive seeds until two
   distinct valid physical layouts exist, then selects one by seed.
4. **Population** is local and makes no model call. The companion derives 20–30
   residents from the accepted topology: named authorities keep their names,
   households share surnames, the person named in `governance` becomes the Reeve,
   occupations follow each workplace's purpose, motivations and fears cite the
   world's own threat, tension, and wound, and relationships include family,
   coworker, authority, and rival ties. Established public facts become dated
   history events. The single `Proposed` change project targets a building the
   central threat affects.
5. **Integrate/repair** deserializes and validates the complete canonical spec.
   Validation issues are persisted with JSON paths. A repair may replace an
   indexed entry or, for section-level issues, a whole section; the request
   carries the section's current content either way. At most three attempts.

Each accepted stage and the final world are stored under
`Saved/WorldRuns/<run-id>/`. `run-summary.json` records every stage, the run
budget, and — once the compiled town's navmesh has finished building — a
`playability` block (navigation built and viable, issues, spawned counts,
terrain archetype, change-project state). Its `outcome` is `Playable` or
`NotPlayable` only after that check; generation completing is not enough.
Runs interrupted by stopping PIE or changing level still write a summary.
The `expression` block lists every model-authored value with its outcome:
`Applied` (it changed the world), `Defaulted` (a fallback chose for it, such as a
location name with no building word), `NotApplicable` (valid but impossible here,
such as a harbor on a basin), or `TextOnly` (prose such as the historical wound,
shown only as text).

## Process and lifetime safety

Every stage launches asynchronously on a worker thread and completes on the game
thread. The completion lambda holds only a `TWeakObjectPtr` to the bridge
subsystem. A run has a total budget (600 s for the CLI provider) and each
stage's timeout is capped by what remains, so retries cannot stretch a run
indefinitely. Inside a stage, the companion makes a corrective retry only if
at least as much time remains as the failed attempt took (minimum 30 s);
`WORLD_DIRECTOR_PROVIDER_MAX_ATTEMPTS` caps attempts (default 2). Timeout
terminates the process tree. Cancellation, game-instance shutdown, PIE stop, and
world cleanup terminate all in-flight processes.

The default companion discovers `codex` on `PATH` and in common Homebrew, local
user, and macOS Codex/ChatGPT application locations, and requires a Codex CLI
with `exec --output-schema` (verified with 0.166.0). Each stage call:

* passes the stage schema as `--output-schema`, projected onto the provider's
  strict structured-output subset (closed objects, all properties required,
  `oneOf` as `anyOf`, no `not`/`uniqueItems`/length keywords). The canonical
  schema is still enforced locally on every response, and the schema text is no
  longer repeated in the prompt;
* runs in an empty temporary working directory with `--ignore-user-config`,
  `--ignore-rules`, `skills.include_instructions=false`, and every tool-bearing
  Codex feature disabled. This cut per-call input from about 19.5k tokens to
  about 12.6k on the interpret stage. Authentication still comes from the
  user's `CODEX_HOME`;
* checks the requested model and reasoning effort against Codex's own
  `models_cache.json` before spending a call, so a retired model fails
  immediately with the list of current ones.

Responses are parsed strictly (duplicate keys and non-standard numbers are
rejected); there is no text-level repair of malformed JSON.

Set `WORLD_DIRECTOR_CLI_COMMAND` to replace the CLI command. The command may use
`{output}` for the last-message file, `{model}`, `{reasoning}`, and `{schema}`
for the output-schema file. A configured command that uses `{schema}` gets the
schema as a file; one that does not gets it in the prompt instead.

The Create World menu lists the models in Codex's catalog, offers reasoning
`low`/`medium`/`high` where the model supports it, and defaults to
`[WorldDirector] DefaultModel` / `DefaultReasoningEffort` in
`Config/DefaultGame.ini` when Codex still serves that model.

The deterministic fixture mode exists only for automated lifecycle and repair
tests. Normal gameplay uses the CLI path.

Normal play exposes this pipeline through the native player creation menu.
Press `N` to create another world in place and `F8` to inspect live AI-stage
metrics, errors, validation repairs, and retained request/response artifacts.
See `PlayerWorldCreation.md` for the gameplay and diagnostics contract.

## Current evidence (2026-10-31, Codex CLI 0.166.0, UE 5.8.4)

* Two live runs with `gpt-6.1-sol` / `medium`, one provider attempt allowed per
  stage: both stages succeeded on the first attempt in both runs (93 s and 96 s
  end to end), interpret/topology input fell from ~19.5k/~20.5k tokens to
  ~12.2k/~13.6k, and both worlds recorded `outcome: Playable` with navigation
  viable and the change project `Scheduled`.
* The alpine-ridge prompt that produced a coast in August
  (`20260830T191343Z-1221F926`) now produced `landform: ridge` and Ridge terrain
  (`20261031T040005Z-A4D971E1`).
* All 15 `WorldDirector.*` automation tests pass, including
  `LandformFromModelCorpus` and `ModelCorpusResolves` over 12 saved real model
  outputs, and route grid fallbacks during the suite went from 36 to 0.
* All in-game fixture flows pass: simulation, project, dialogue, travel,
  prompted and blank generation, timeout, cancellation, player flow, saved world.

## Historical evidence (August 2026, before the October fixes)

The headless acceptance map verified:

* prompted fixture generation: playable, 24 residents;
* blank fixture generation: playable, 24 residents;
* forced invalid population entry: one targeted repair, then playable;
* forced three-second companion stall: terminated by a one-second deadline;
* in-flight cancellation: process terminates and no companion remains;
* real prompted Codex CLI run `20260822T094714Z-137C4ADA`: playable, 20
  residents;
* real blank Codex CLI run `20260822T095055Z-FFB64CCB`: invented Thornwake with
  20 residents. It exposed and led to a compiler fix for public-purpose shell
  classification; replaying that exact accepted spec then passed all eight door
  probes and the full navigation gate;
* fresh post-fix blank Codex CLI run `20260822T100146Z-E0850D7C`: invented
  Cinderwell without asking questions, persisted non-ASCII model text as UTF-8,
  and passed generation, compilation, all door probes, and the 20-resident
  physical navigation gate in one run;
* Phase 6 prompted run `20260822T105549Z-2105EB0B`: invented Brackenford with 18
  locations and 20 residents, passed the full-slice social contract, targeted
  repair, compilation, and physical navigation;
* Phase 6 blank run `20260822T110043Z-8CAA6AC1`: invented Gloamwatch without
  clarification, with 16 locations and 24 residents. Its unchanged accepted
  spec passed replay after generated barracks/shelter households exposed and led
  to a building-agnostic resident-spawn fix.

Example real-provider acceptance command:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" \
  /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nosplash -nullrhi -nomcp \
  -WorldDirectorGenerationAutoTest -WorldDirectorPromptedGeneration \
  -WorldDirectorUseCliProvider
```
