# Director Inspection and Hardening

Phase 8 adds an optional native UMG inspection panel to the compiled town.
Press `I` while playing to open or close it. The panel is diagnostic rather
than a player-facing save interface and reads the canonical world state plus
the live simulation, project, and generation bridge state.

## Causal inspection

The panel exposes accepted facts; every relationship; resident belief IDs and
memory summaries; location purpose, ownership, controller, and access; accepted
generation stages; validation issues and repair count; current project states;
recent authored events; and resident schedule decisions. Each registered
project retains a proposal summary, its complete lifecycle, the terminal Unreal
decision, and semantic before/after location state. A replayed fixture still
shows the complete world and simulation state while explaining that generation
stage files live under `Saved/WorldRuns`.

The interface uses a restrained dark-and-gold treatment and contains no live
status animation. It is created entirely in native code, so it does not add a
hand-authored Blueprint dependency to generated worlds.

## Automated hardening coverage

The validation and runtime gates cover:

| Failure class | Enforcement |
|---|---|
| Invalid home/work/fact/resident references | semantic `reference.missing` errors |
| Housing capacity | `household.capacity_exceeded` |
| Unsupported purposes/capabilities | `capability.tag_missing` and certified compiler/project policies |
| Relationship inconsistency | reciprocal relationship validation |
| Orphaned secrets | `secret.orphaned` |
| Repeated resolved shell overuse | `compiler.asset_overuse` warning |
| Inaccessible entrances and schedule routes | compiled navigation viability gate |
| Failed live project application | forced missing-interior transition reaches `Failed` |

Recent staged requests, raw provider responses, accepted stage payloads,
validation reports, and integrated specs continue to be retained in
`Saved/WorldRuns/<run-id>/` for exact replay and diagnosis.

## Verification

Run the native automation group:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" -unattended -nullrhi -nosound -nosplash \
  -ExecCmds="Automation RunTests WorldDirector;Quit" \
  -TestExit="Automation Test Queue Empty"
```

Run the live causal inspection check:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nullrhi -nosound -nosplash -nomcp \
  -WorldDirectorInspectionAutoTest
```

The required marker is `WORLD_DIRECTOR_INSPECTION_RESULT=PASS`. The same run
also requires project evidence for `Active`, `Refused`, `Delayed`, and `Failed`
decisions and a post-transition navigation pass.
