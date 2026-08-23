# Dynamic World Edits

Phase 7 implements one runtime edit primitive: `RepurposeLocationProject`.
The AI population stage must propose exactly one project motivated by an
existing generated threat. The proposal remains semantic and contains an
initiator, target location, desired purpose, reason, required participants,
broad capabilities, conditions, intended simulation minute, transition time,
and initial `Proposed` state. It never contains asset paths or transforms.

## Independent Unreal decision

`UChangeProjectSubsystem` owns the running project state and independently
checks:

* the source-to-purpose conversion is certified by the capability pack;
* the target was compiled under the repurposable policy;
* the owner/controller is the initiator or a required participant;
* every required participant exists and can path to the target entrance;
* every required broad capability is registered;
* `Condition.ThreatActive` refers to a world with an initial threat;
* overnight timing and the 60–1440 minute transition bound are valid;
* the player is away and unrelated occupants are absent at transition time.

The implemented conversion table is deliberately narrow:

| Current purpose | Desired purpose | Runtime dressing |
|---|---|---|
| Home | Clinic | certified small-home bed/work dressing |
| Workplace | Shelter | certified small-home bed/seating dressing |
| Shelter | Headquarters | certified workplace counter/table dressing |
| Headquarters | Workplace | certified workplace counter/table dressing |

Invalid authority or conversion is `Refused`; temporary participant,
navigation, player-proximity, or occupancy conditions produce `Delayed`;
unrecoverable runtime/application errors produce `Failed`.

## Lifecycle and live transition

Accepted projects move through:

```text
Proposed -> Validated -> PermissionResolved -> Scheduled
         -> Preparation -> Transition -> Active
```

Preparation starts at the proposed evening minute. A time skip may cross that
minute; the evaluator uses the scheduled minute rather than incorrectly judging
availability from the end of a coarse skip. The transition occurs only after
the bounded preparation duration and while the player/other occupants are away.

Activation mutates the already-spawned location actor in place. It changes the
canonical purpose, controller, access policy, certified capability list,
interior actor, visible purpose sign, and Smart Object stations. Required
participants receive an updated evening schedule stop, a belief referencing an
existing generation-time fact, and a memory of the project. Home conversions
relocate the affected household to a compatible existing home or shelter; no
new fact or asset is invented at runtime. The active spec is synchronized to
`UWorldStateSubsystem` and the autonomous simulation without a reload, cook,
level write, or `.uasset` edit.

## Acceptance evidence

The authored `project.emergency_workshop`, motivated by the mill-failure threat,
turns `location.commons_shed` from public shelter into restricted repair
headquarters. The dedicated runtime check reports:

* all seven lifecycle states through `Active`;
* the same target actor before and after;
* dressing revision 1, a visible sign, and 20 replacement work/meeting stations;
* controller/access, participant schedule, belief, and memory changes;
* physical navigation PASS after redressing;
* a second unsupported/unauthorized proposal `Refused`;
* a valid occupied/player-nearby proposal `Delayed`.
* a supported proposal whose live interior is deliberately removed before
  application reaches `Failed` with `project.location_not_live`.

Real provider run `20260822T113213Z-21DBE0E9` independently generated Debtwater,
its cracked-sluice threat, and `project.granary_emergency_shelter`. The model
selected the controller as a required participant and proposed converting the
Common Granary from workplace to overnight flood shelter. Replaying the exact
accepted spec produced 12 replacement shelter stations, visible shelter
signage, public access, new social consequences, and navigation PASS.

Run the authored lifecycle check:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nosplash -nullrhi -nomcp -WorldDirectorProjectAutoTest
```

Pass markers are `WORLD_DIRECTOR_PROJECT_EVIDENCE ... navigation=PASS` and
`WORLD_DIRECTOR_PROJECT_RESULT=PASS`.
