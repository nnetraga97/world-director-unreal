# Player World Creation and AI Diagnostics

Normal play now starts on `L_WorldDirectorTown` with a native **Create a Living
World** menu instead of compiling the authored fixture. The player may enter a
prompt or leave it blank, choose a reproducible positive seed, and select
the Codex model and reasoning effort before selecting **Create World**. The
default balanced selection is `gpt-5.6-terra` with `medium` reasoning. The
default path invokes the real local CLI companion. The clearly labelled debug
checkbox uses the deterministic fixture and never calls a model.

The menu remains responsive while the director runs the asynchronous
`interpret -> topology -> layout -> population -> integrate/repair` pipeline.
It exposes the current stage, elapsed time, accepted stages, repair count, and
validation-issue count. **Cancel** terminates the companion process. A failed
run leaves the menu open with a useful error and preserves its run artifacts.

On success, the runtime coordinator resolves the accepted specification,
tears down the prior town and its residents/Smart Objects only after semantic
resolution succeeds, compiles the new town in place, installs canonical world
state, and starts the living-town simulation. Press `N` during play to create
another world without a level reload. The creation menu uses UI-only input
while its text field is active, has a Close action whenever generation is idle,
and restores first-person input when dismissed. Press `Tab` during play to
toggle cursor interaction for clicking residents, then press it again to return
to mouse-look.

## AI diagnostics

Press `F8` or choose **AI Diagnostics** from the creation menu. `F8` avoids
colliding with WASD movement or prompt typing. The drill-down view refreshes
during generation. Use **Previous stage** and **Next stage**, then switch among
**Request**, **Prompt**, **Response**, **Events**, and **Telemetry** to inspect
the complete retained artifacts for that exact model call. The summary reports:

* provider, selected model, reasoning effort, provider thread, and run identity;
* current state, seed, prompt size, and total elapsed time;
* per-stage start time, latency, request/response byte counts, process exit
  code, input/cached/output/reasoning tokens, retained artifact paths, errors,
  and bounded provider output;
* selected layout, targeted validation issues, and repair attempts;
* a timestamped bounded event log;
* the exact retained run directory.

The view can copy the currently visible full artifact to the clipboard or open
the run folder.
Its Close action and the `F8` toggle restore the appropriate underlying input
mode.
Every finished or cancelled run also writes `run-summary.json` beside the
existing request, constructed prompt, raw model response, structured response,
raw JSONL provider events, accepted-stage, validation, and integrated-world files. Token counts
come directly from Codex's `turn.completed` usage event. The authenticated Codex
CLI does not emit a monetary charge, so diagnostics explicitly labels
provider-billed cost as unavailable instead of inventing spend or applying a
potentially stale API price. It does not expose hidden model reasoning.

## Verification

The fast player-loop gate uses the deterministic provider while exercising the
same menu, callback, compile, simulation, navigation, diagnostics, and retained
summary path. It creates two worlds in one session and verifies that the first
town is destroyed before the replacement becomes active:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nullrhi -nosound -nosplash -nomcp \
  -WorldDirectorPlayerFlowAutoTest
```

The required marker is `WORLD_DIRECTOR_PLAYER_FLOW_RESULT=PASS`. Add
`-WorldDirectorPlayerFlowUseCliProvider` to exercise the real authenticated
CLI through this same player-facing path.

Real-provider run `20260822T140747Z-4BB74C7A` exercised that exact path. It
generated Brackenford in 314.68 seconds: 18 locations, 24 residents, 48
directed relationships, one threat, and one mill-emergency headquarters
project. Unreal rejected the initial population with 24 precise resident social
state issues, accepted one targeted repair without regenerating topology or
layout, compiled the repaired town, and passed physical navigation. The fast
gate then created two fixture-backed worlds in one session and reported
`generations=2 regeneration=PASS`.
