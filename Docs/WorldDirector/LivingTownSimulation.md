# World Director Living Town Simulation

Phase 4 introduced a 24-resident town that remains coherent without a connected
model. Phase 6 expands `living-town.json` to 18 locations while retaining six
households and 24 residents, and connects them with 46 reciprocal directed
relationships, structured beliefs, one initial threat, and a closed table of
six world facts. The runtime compiler resolves that semantic state into the
certified Stylized Medieval Frontier capability pack.

## Resident behavior

Every spawned resident owns a `UStateTreeComponent` configured with
`/Game/WorldDirector/AI/ST_ResidentLife`. Its long-running resident-life task
delegates schedule resolution and movement intent to `UTownSimulationSubsystem`.
The subsystem is authoritative for the clock and canonical off-screen state;
StateTree and AI movement make the current intent visible in the world.

Residents resolve a small daily vocabulary: sleep at home, work at their
workplace, meet socially, or wait. Each activity targets a gameplay-tagged
Smart Object. Claims are held by resident ID and explicitly released when the
intent changes. If the preferred slot is occupied, the resident tries another
compatible station and then enters a bounded wait state. Contention is therefore
expected behavior rather than a stalled AI state.

## Clock and off-screen resolution

The clock exposes a configurable number of in-game minutes per real second.
Schedule transitions use that clock, and the movable directional light follows
the same time of day. `AdvanceSimulationMinutes` is the explicit time-skip path:
it advances canonical resident state, relationship events, and belief sharing
without requiring every skipped movement to play in real time.

The simulation may run with no director connection. Runtime code cannot append
facts; beliefs and revelations may reference only IDs from the generation-time
closed fact set.

## Social rule

At a shared meeting activity, a resident can transfer one shareable belief when
the relationship trust and belief willingness thresholds pass. The listener
stores the speaker as source and receives lower confidence. The current fixed
event set adjusts Trust, Affinity, Fear, and Obligation only; it is intentionally
not a general social simulation.

## Physical integration assumption

The imported vendor interiors do not expose stable semantic furniture socket
names, and their interior navmesh can form disconnected islands. For this
vertical slice, invisible activity Smart Objects are placed on the connected
exterior threshold nearest each compiled semantic entrance. They preserve real
claim/release, contention, navigation, schedules, and co-location behavior, but
they do not yet prove that a resident reaches the visible bed, chair, counter,
or work surface. Authoring stable sockets and requalifying interior navigation
is required before claiming furniture-level interaction.

## Verification

The Phase 4 fixture passes four independent gates:

1. `WorldDirector.Phase4.LivingTownFixture` validates all 24 residents and the
   richer relationship, belief, memory, availability, and location fields.
2. The physical gate reports
   `WORLD_DIRECTOR_NAVIGATION_RESULT=PASS locations=18 residents=24` at all four
   quarter-turn orientations after every entrance door probe passes.
3. The accelerated three-day gate reports
   `WORLD_DIRECTOR_SIMULATION_RESULT=PASS`, including 24 workers, explicit
   meeting contention, 24 sleepers, shared beliefs, relationship events, and an
   unchanged six-fact closed set.
4. The real-time gate reports `WORLD_DIRECTOR_TRAVEL_RESULT=PASS` after all 24
   residents move at least 150 cm under running StateTrees.

Run the full automation suite:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "$PWD/WorldGen/WorldGen.uproject" -unattended -nop4 -nullrhi -nosplash -nomcp \
  -ExecCmds="Automation RunTests WorldDirector; Quit" \
  -TestExit="Automation Test Queue Empty"
```

Run the three-day and real-time gates against the authored map:

```sh
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nosplash -nullrhi -nomcp -WorldDirectorSimulationAutoTest

"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor" \
  "$PWD/WorldGen/WorldGen.uproject" /Game/WorldDirector/Maps/L_WorldDirectorTown \
  -game -unattended -nosplash -nullrhi -nomcp -WorldDirectorTravelAutoTest
```

The daylight visual capture confirms the intended architecture and sky, but the
terrain still uses the capability Landscape's checker material and residents
are not legible in the wide audit framing. Those are presentation gaps, not
accepted environment polish.
