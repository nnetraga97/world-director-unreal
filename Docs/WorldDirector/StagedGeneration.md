# World Director Staged Generation

Phase 5 uses a replaceable `IWorldDirectorProvider` implementation backed by a
one-shot local companion process. The companion invokes the configured CLI
agent for one semantic stage at a time; Unreal remains the authority for
capabilities, candidate layout seeds, validation, asset resolution, compilation,
and physical viability.

## Stage contract

1. **Interpret** returns a supported world brief. A blank prompt is explicitly
   treated as permission to invent an original setting without asking questions.
2. **Topology** returns settlement identity, terrain preferences, districts,
   important locations and adjacency, governance, historical wound, current
   tension, supernatural premise, central threat, initial facts, and threats.
   It cannot return asset paths or transforms.
3. **Layout** receives four Unreal-authored candidates identified only by opaque
   IDs and semantic summaries. The selected candidate controls the compiler seed.
4. **Population** fills residents, households, occupations, reciprocal
   relationships, motivations, memories, beliefs, secrets, tension links, and
   exactly one threat-motivated `Proposed` repurpose project. Unreal decides
   whether that project is compatible and executable after the town is live.
5. **Integrate/repair** deserializes and validates the complete canonical spec.
   Validation issues are persisted with JSON paths. A repair response may replace
   only an indexed top-level entry named by those errors. Valid sections cannot
   be regenerated. At most three attempts are accepted; the final request tells
   the provider to substitute the closest supported detail if a failure repeats.

Each accepted stage and the final world are stored under
`Saved/WorldRuns/<run-id>/`. Requests and raw responses are retained beside the
accepted files for diagnosis.

## Process and lifetime safety

Every stage launches asynchronously on a worker thread and completes on the game
thread. The completion lambda holds only a `TWeakObjectPtr` to the bridge
subsystem. Each process has an explicit per-stage deadline; the full-slice CLI
default is five minutes because the connected 20–30 resident population payload
is substantially larger than earlier fixtures. Timeout terminates the process
tree. Explicit cancellation, game-instance shutdown, PIE stop, and world
cleanup terminate all in-flight processes. World cleanup clears delegates before
destroyed actors can receive a completion.

The default companion discovers `codex` on `PATH` and in common Homebrew, local
user, and macOS Codex/ChatGPT application locations. This also works when Unreal
is launched from Finder with a minimal `PATH`; the companion also supplies the
standard package-manager runtime paths needed by script-based CLI launchers. It
runs an ephemeral, read-only, no-approval request and uses `--ignore-user-config`
so project generation is not coupled to optional user-agent configuration;
authentication is still inherited.
Set `WORLD_DIRECTOR_CLI_COMMAND` to replace the CLI command. The command may use
`{output}` as the last-message file placeholder, `{model}` for the player-selected
model, and `{reasoning}` for the selected reasoning effort. The bundled Codex
path records its JSONL event stream and exact token usage for diagnostics.

The deterministic fixture mode exists only for automated lifecycle and repair
tests. Normal gameplay uses the CLI path.

Normal play exposes this pipeline through the native player creation menu.
Press `N` to create another world in place and `F8` to inspect live AI-stage
metrics, errors, validation repairs, and retained request/response artifacts.
See `PlayerWorldCreation.md` for the gameplay and diagnostics contract.

## Current evidence

The headless acceptance map verifies:

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
