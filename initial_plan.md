# Handoff Execution Plan

Unreal Engine, Xcode, the MCP server, and agent permissions are working. The
project itself is deliberately near-empty: it is the stock First Person
Blueprint template with no C++ module, no plugins beyond the defaults, and no
art content. Phase 0 and Phase 1A exist to close that gap. Do not assume any
content or code exists until you have verified it.

## Mission

Build a vertical slice of an Unreal Engine 3D RPG sandbox where:

1. The player enters an optional world prompt or leaves it blank.
2. An AI director creates a coherent dark-fantasy town specification.
3. Unreal constructs the physical town exclusively from preselected authored assets and building shells.
4. Approximately 20–30 NPCs live in the town with homes, occupations, schedules, relationships, beliefs, memories, and motivations.
5. The deterministic simulation operates independently of the AI.
6. During play, the director may propose a justified world-change project.
7. Unreal validates and executes that project, visibly changing a location, NPC routines, access, or social state.

Do not build a full RPG. Prove world generation, living-town simulation, and dynamic world evolution.

---

## Non-negotiable architecture

The agent has full Unreal Editor MCP and filesystem access. Use them freely for development and debugging.

However, generated playthroughs must use this pipeline:

```text
AI semantic world specification
            ↓
Schema and continuity validation
            ↓
Unreal physical resolution
            ↓
Approved authored assets
            ↓
Runtime world state
```

Do not make generated worlds by having the runtime director arbitrarily edit actors, Blueprints, source code, or `.uasset` files.

### Construction model: fully runtime

This is a binding decision. Every other technical choice follows from it.

* The town is built **entirely at runtime**, by spawning actors and components
  into a live world. Nothing generated is ever written to a `.uasset`, a level,
  or an External Actor file.
* There is no editor-time town authoring step, no baked layout, and no
  generated content in source control. A generated town is transient state.
* Lighting is fully dynamic. Static lighting stays disabled.
* Navigation is generated at runtime (see Phase 3).
* PCG must be restricted to nodes that support runtime generation. If a PCG
  feature only works at editor time, do not use it.
* Regeneration must not require a level reload. Tearing down the current town
  and building a new one from a new specification happens in place, in a live
  world.
* Phase 7 world changes apply to already-spawned actors in the running world.
  No reload, no re-cook, no editor round trip.

The only content that ships as assets is the **capability pack**: authored
meshes, interiors, characters, props, and the game-owned Data Assets describing
them. The generator consumes that pack; it never adds to it.

Note that the level uses World Partition with One File Per Actor. World
Partition streaming grids do not manage runtime-spawned actors. Treat the
generated town as living in the always-loaded space and budget accordingly.

### Division of authority

The AI determines:

* Setting and history
* Town identity
* District and location purposes
* NPC identities and households
* Occupations
* Relationships
* Motivations, fears, beliefs, and secrets
* Ownership and access
* Social tensions
* Threat premise
* Proposed future projects

Unreal determines:

* Exact assets
* Coordinates and rotations
* Roads and plots
* Building compatibility
* Interior templates
* Character-part selection
* Prop placement
* Navigation
* Exact schedule times
* Whether proposed changes are mechanically possible
* Actual outcomes

The director proposes pressures and intentions. The simulation determines outcomes.

---

# Phase 0 — Make the project buildable

The project is Blueprint-only with no `Source/` directory. Nothing in Phase 2
onward can start until this is done.

Tasks:

* Convert `WorldGen` to a C++ project and confirm a clean compile on macOS
  (Xcode 26.4, UE 5.8).
* Enable the plugins the plan depends on: **PCG** and **SmartObjects**.
  `GameplayStateTree` is already enabled. Confirm `AIModule` and
  `GameplayBehaviors` availability for StateTree AI integration.
* Add a standard Unreal `.gitignore` (`Binaries/`, `Build/`,
  `DerivedDataCache/`, `Intermediate/`, `Saved/`, `.DS_Store`, `*.xcworkspace`)
  and make the first commit before any art content lands. Set up Git LFS for
  `*.uasset` and `*.umap` at the same time — after the capability pack is
  imported it is too late.
* Confirm the MCP server at `http://127.0.0.1:8000/mcp` responds.

### Exit condition

The project compiles from a clean state, PCG and SmartObjects load, and the
repository has an initial commit with LFS configured.

---

# Phase 1A — Acquire the capability pack

There is currently no art content. The buildable inventory is thirteen static
meshes, seven of which are grey-box primitives. Acquiring content is a real
deliverable, not an inspection task.

**Budget is zero.** Everything below is free and license-clean for commercial
use. Do not introduce a paid dependency without escalating first.

The single hardest requirement is **enterable interiors**. Most free medieval
packs are exterior shells only, which would make Phase 6's "every occupied
building enterable" unreachable. Filter on that before anything else.

Second requirement: buildings must come from **one coherent architectural
family** with several substyles, because Phase 6 requires exactly that.

### Recommended acquisition — Quaternius (CC0)

Quaternius covers every category this plan needs, in one consistent stylized
art family, under CC0 (free for commercial use, no attribution required). Take
the whole stack from one source rather than mixing vendors — art coherence is
a Phase 6 acceptance requirement.

| Need | Pack | Why |
|---|---|---|
| Buildings + interiors | **Medieval Village MegaKit** (300+ models) | Modular roofs, stairs, walls, doors, windows. Critically, **the walls have an exterior and an interior surface on the same mesh**, so buildings are enterable by construction rather than by luck. This single property is why the free route is viable at all. |
| Second substyle | **Modular Medieval Building Pack** | Gives Phase 6 its "several substyles from one regional family" without leaving the art family. |
| Landmark | **Ultimate Modular Ruins Pack** | Satisfies the "at least one landmark" requirement. |
| Cellars / secret sites | **Modular Dungeons Pack** | Phase 7 lists `Cellar → secret meeting site` as a supported conversion. This is that content. |
| Character base | **Universal Base Characters** (rigged, retargetable) | The skeleton everything else binds to. |
| Character parts | **Ultimate Modular Men Pack** + **Ultimate Modular Women Pack** | Each character splits into four swappable models. This *is* the "character parts" system the capability catalog needs — combinatorially enough for 20–30 distinct residents. |
| Occupation visuals | **Modular Character Outfits – Fantasy** (12 outfits, 62 modular parts) | Compatible with Universal Base Characters. Outfit swap is how an NPC reads as a smith versus a priest. |
| Smart Object anchors | **Ultimate House Interior Pack** + **Ultimate Furniture Pack** | Beds, chairs, tables, work surfaces — the four anchor types Phase 4 depends on. Prop coverage of those four is the acceptance test for this step. |
| Clutter and signage | **Fantasy Props MegaKit**, **RPG Essentials Pack** | PCG dressing and the Phase 7 signage change. |

Sources: `quaternius.com` and `quaternius.itch.io`. The free tier ships
`.FBX`, `.OBJ`, and `.glTF`. The paid "Source" tier adds `.blend` files and
engine-specific shader setups — **not required**; do not buy it.

### Alternative / supplement — KayKit (CC0)

`kaylousberg.itch.io` — Medieval Builder Pack, Dungeon Pack, furniture "Bits"
bundles, and character packs, all CC0, all cross-compatible with each other,
shipping FBX/OBJ/DAE/glTF. Use as a fallback if a Quaternius pack turns out
thin in a category. **Do not mix the two families inside one town** — pick one
and stay in it.

### Rejected options, and why

* **Epic's Infinity Blade and Paragon packs** are genuinely permanently free
  and high quality, but the environments are non-modular, have no enterable
  interiors, and are stylistically incompatible with anything else here. They
  fail the primary filter.
* **XS Gamestudios "Sunset"** (120+ modular medieval parts, 40+ interior and
  furniture meshes) was free only during May 2022. If it was claimed then it is
  owned permanently and is a strong realistic-style option — check the Fab
  library first. Otherwise it is unavailable.
* **Marco Maria Rossi's Medieval Village** (496 meshes) was Fab
  limited-time-free through 30 December 2025, same situation — check the
  library, and note it has no interiors regardless.
* Paid Fab packs with prebuilt furnished interiors were evaluated and set aside
  under the zero-budget constraint.

### Terrain

Do not download terrain. Author a Landscape by hand: roughly 2×2 km, gentle
slopes, one large flat buildable basin, one elevated area for a landmark.
Hand-authoring is free, faster than integrating a terrain pack, and gives full
control over the buildable mask that Phase 5 stage 3 depends on.

### Known costs of the free route

These are real and must be budgeted, not discovered later:

* **Import and material work.** These packs ship as FBX/glTF, not `.uasset`.
  Every mesh needs import, collision setup, and a material pass. There is no
  Nanite/Lumen-tuned material set out of the box. This is the largest single
  task in Phase 1A.
* **The art is stylized low-poly, not realistic.** Adapt the theme accordingly
  and state plainly in the docs that this is a stylized slice. Strike any
  realism claim from the acceptance criteria.
* **Character rigs are not the UE5 Manny skeleton.** Quaternius characters
  arrive animated with their own rig. Decide early and record the decision:
  either use the bundled animation set (simpler, smaller vocabulary), or build
  an IK Rig and retarget UE5 animations onto the Quaternius skeleton (more
  work, richer motion). Phase 4 behavior depends on which way this goes.
* **Collision must be authored.** Free packs rarely ship usable complex
  collision. Doors, walls, and stair navigation all depend on it.

### Exit condition

The capability pack is imported into `Content/CapabilityPack/` and contains:
one authored Landscape; at least eight distinct enterable building shells
across at least two substyles; working interior geometry; a modular character
system demonstrated to produce 20–30 visually distinct residents; and props
covering beds, work surfaces, chairs, and counters. Every imported mesh has
collision and a material.

---

# Phase 1B — Certify the content

Inspect what Phase 1A produced and build a capability catalog covering:

* Terrain envelopes
* Building shells
* Interior templates
* Doors and entrances
* Workplaces
* Beds and household capacity
* Landmark types
* Character parts
* Occupation visuals
* Location states
* Supported location conversions
* Smart Object opportunities

Do not modify vendor assets directly. Wrap them in game-owned Blueprints or Data Assets.

Each building variant needs metadata such as:

```text
Semantic archetype
Architectural style
Footprint
Entrance sockets
Resident capacity
Supported purposes
Supported location states
Compatible interiors
Slope tolerance
Navigation requirements
```

Produce:

```text
Content/CapabilityPack/
Saved/WorldDirector/asset-audit.json
Docs/WorldDirector/CapabilityPack.md
```

### Exit condition

Enough certified content exists to construct a small test settlement with
enterable homes and workplaces. Every entry in the catalog points at a real
asset that has been opened and confirmed, not inferred from a filename.

When the acquired asset quality suggests a better theme than the current
dark-fantasy frontier concept, adapt the final theme while preserving the same
technical architecture.

---

# Phase 2 — Build the canonical data model

Create a `WorldDirector` C++ plugin with runtime, editor, and test modules.

Core systems:

* `UCapabilityCatalogSubsystem`
* `UWorldGenerationSubsystem`
* `UWorldStateSubsystem`
* `UTownSimulationSubsystem`
* `UChangeProjectSubsystem`
* `UDirectorBridgeSubsystem`

Create versioned C++ structures and matching JSON Schemas for:

* `WorldBrief`
* `TownTopology`
* `GeneratedWorldSpec`
* `ResolvedWorldPlan`
* `Resident`
* `Household`
* `Relationship`
* `Belief`
* `WorldFact`
* `WorldEvent`
* `Threat`
* `ChangeProject`
* `ValidationReport`

Use stable IDs for all entities. Never use names as identifiers.

Required invariants:

* Every NPC has a valid home.
* Every employed NPC has a valid workplace.
* Every location has a purpose, owner or controller, and access policy.
* Every referenced ID exists.
* Household occupancy fits building capacity.
* Relationship references are internally consistent.
* Mechanical tags exist in the capability catalog.
* Important secrets reference established world facts.
* No runtime revelation may retroactively invent a past fact.
* AI-facing data contains no Unreal asset paths.

### Exit condition

A hand-authored JSON world can be loaded, validated, rejected with useful errors, and serialized back without data loss.

---

# Phase 3 — Build the world compiler

Before introducing AI, compile a hand-authored fixture containing:

* One terrain envelope
* Six to eight buildings
* Six to eight NPCs
* Homes and workplaces
* Enterable interiors
* Roads and paths
* Working doors
* Basic schedules
* Valid navigation

Compilation pipeline:

```text
GeneratedWorldSpec
        ↓
Layout and placement
        ↓
Exact asset resolution
        ↓
ResolvedWorldPlan
        ↓
Spawn world
        ↓
Navigation and viability checks
```

Seeds are used for variation and for reproducing a layout while debugging.
Bit-identical reproducibility is **not** a requirement of this project. Do not
spend effort making PCG, navigation, or physics deterministic, and do not gate
any phase on a determinism test.

Use PCG for environmental dressing, vegetation, fences, paths, and
state-dependent clutter, restricted to runtime-capable nodes. Do not put
narrative logic inside PCG.

### Building instancing policy

This follows directly from the runtime construction model and from Phase 7's
requirement that a location can be re-dressed while the game is running.

* Buildings flagged as **repurposable** spawn their interiors as individual
  actors and components, so dressing, signage, and Smart Objects can be swapped
  live.
* All other static geometry is merged into Instanced Static Mesh components at
  spawn time for draw-call cost.
* Do not use Packed Level Actors anywhere in the generated town. Packed
  geometry cannot be re-dressed per instance and is an editor-time construct,
  which conflicts with both the runtime model and Phase 7.

### Navigation

Keep navigation basic for now. A single generously sized NavMeshBoundsVolume
covering the buildable basin, with runtime dynamic generation enabled, is
sufficient. Accept the rebuild cost. Do not build nav invokers, partitioned
navmesh, or streaming nav until something actually demands it.

Add one assertion after spawn: every resident can path home → workplace →
landmark. That is the whole navigation acceptance bar for now.

### Exit condition

The fixture repeatedly creates a playable town without manual editor intervention, overlapping structures, inaccessible doors, stranded NPCs, or invalid references.

Do not proceed to AI generation until this works reliably.

---

# Phase 4 — Add living NPC simulation

Use normal Unreal actors for 20–30 NPCs. Do not introduce MassEntity unless profiling demonstrates a real need.

Use:

* StateTree for behavior
* Smart Objects for beds, workstations, chairs, counters, meeting positions, and other activities
* Gameplay Tags for occupations, activities, access, and location purposes

Smart Objects need a claim and release model. Two NPCs wanting the same counter
is the normal case, not the edge case. An NPC whose target Smart Object is
claimed must pick an alternative or wait, not stall.

Each NPC requires:

* Home
* Occupation or dependency
* Daily schedule intent
* Current location
* Relationships
* Motivation
* Fear
* Structured beliefs
* Important memories
* Availability state

Keep relationship mechanics small:

* Trust
* Affinity
* Fear
* Obligation

Beliefs should record:

* Referenced fact
* Source
* Confidence
* Secrecy
* Emotional significance
* Willingness to share

### Time of day

The town needs a clock before schedules mean anything.

* An in-game clock with a configurable time scale.
* A day/night cycle driving lighting, so "morning" and "night" are observable.
* Schedules resolve against this clock.
* A time-skip path that advances the clock and resolves off-screen state, which
  Phase 7's overnight changes depend on.

### Belief propagation

Keep this deliberately minimal for now. One rule is enough:

> When two NPCs are co-located at a Smart Object and the speaker's Trust in the
> listener exceeds a threshold, the speaker shares one belief whose
> `WillingnessToShare` clears that threshold. The listener gains the belief at
> reduced confidence, with the speaker recorded as the source.

Relationship values change on a small fixed set of events. Do not build a
social model beyond this until the basic loop is visibly working.

`WorldFact` is a closed set fixed at generation time. Revelations reference
existing fact IDs only. Runtime may never append to the fact table — that is
how the "no retroactive invention" invariant is actually enforced.

The simulation must continue when no model is connected.

### Exit condition

The small fixture can run through multiple in-game days. NPCs travel between valid homes and workplaces, use appropriate Smart Objects, and maintain coherent state.

---

# Phase 5 — Integrate staged AI world generation

Implement the first director provider through a local companion process that invokes the configured CLI agent. Keep the provider interface replaceable.

All model operations must be asynchronous. Never freeze Unreal while waiting for generation.

Two requirements that are not optional:

* **Lifetime safety.** Every async completion callback must be guarded by a
  weak pointer to its owning object, and every in-flight request must be
  cancelled on world teardown and on PIE stop. An async callback landing in a
  destroyed subsystem is the most likely crash in this project.
* **Timeouts.** Every generation stage has an explicit timeout. A hung CLI
  process must fail the stage, not hang the editor.

Generation stages:

### 1. Interpret or imagine

When the player provides a prompt, convert it into a supported world brief.

When the prompt is blank, confidently invent an original world from the capability catalog and seed. Do not ask the player questions.

### 2. Generate semantic topology

Create:

* Settlement identity
* Terrain preferences
* Districts
* Important locations
* Adjacency constraints
* Governance
* Historical wound
* Current tension
* Supernatural premise
* Central threat

Do not generate transforms or raw assets.

### 3. Generate physical layout candidates

Unreal generates multiple valid candidates from authored terrain envelopes, buildable masks, roads, plots, and certified shells.

The agent may inspect summaries or previews and select a candidate. Candidates
are identified by opaque IDs and described without asset paths.

### 4. Generate the population

Create all residents, households, occupations, relationships, motivations, knowledge, beliefs, secrets, and connections to the town’s tensions.

### 5. Integrate and repair

Unreal validates the complete specification and returns targeted errors with JSON paths.

The agent repairs invalid sections only. Do not regenerate the entire world because one household or location is invalid.

Limit repair attempts. When a specific generated detail repeatedly fails, replace it with the closest supported alternative.

Store each accepted stage in:

```text
Saved/WorldRuns/<run-id>/
```

### Exit condition

Both prompted and blank generation can produce a small playable town without manual JSON editing.

---

# Phase 6 — Scale to the full vertical-slice town

Expand generation and compilation to:

* 20–30 NPCs
* Approximately 12–18 buildings
* Several architectural substyles from one coherent regional family
* Every occupied building enterable
* Homes, workplaces, public locations, and at least one landmark
* A complete relationship network
* Structured beliefs and memories
* One initial central threat

Do not add combat, inventory, crafting, skill trees, or a detailed economy.

Dialogue should remain lightweight and menu-based. It only needs to expose world facts and a few social actions.

### Exit condition

Repeated generated towns are physically valid, socially connected, and visibly different in layout, population, ownership, history, and tension.

---

# Phase 7 — Implement one dynamic world-edit primitive

Implement one deep system:

## `RepurposeLocationProject`

Supported examples:

```text
Residence → temporary clinic
Warehouse → shelter
Cellar → secret meeting site
Closed workshop → active workshop
Unused room → faction headquarters
```

The director proposes:

* Initiator
* Target location
* Desired purpose
* Reason
* Required participants
* Required conditions
* Intended timing

Unreal evaluates:

* Location compatibility
* Ownership or authority
* Occupancy
* Participant availability
* Broad required capability
* Physical access
* Required transition time

Project lifecycle:

```text
Proposed
→ Validated
→ Permission resolved
→ Scheduled
→ Preparation
→ Transition
→ Active, delayed, refused, or failed
```

A successful transition must change several systems together:

* Location purpose
* Ownership or controller
* Access policy
* Interior dressing
* Signage
* Smart Objects
* Occupants
* NPC schedules
* Relevant beliefs and memories

Major visual changes should occur overnight or while the player is away rather
than appearing instantly. This uses the Phase 4 time-skip path. Because the
target building was spawned under the repurposable policy from Phase 3, the
change is applied to live actors in place — no reload, no asset edits.

### Exit condition

The AI identifies a believable world pressure and proposes a location project. Unreal independently accepts, modifies, delays, or rejects it. The result becomes visibly and socially apparent in the town.

---

# Phase 8 — Add inspection and hardening

Create an optional director-inspection interface showing:

* Accepted world facts
* NPC relationships
* Beliefs and memories
* Location ownership and purpose
* Generation stages
* Validation failures and repairs
* Current projects
* Recent events
* Director proposal
* Simulation decision
* Before-and-after state

Add automated checks for:

* Invalid references
* Missing homes or workplaces
* Capacity violations
* Inaccessible entrances
* Unreachable schedule destinations
* Unsupported location purposes
* Relationship inconsistencies
* Orphaned secrets
* Repeated asset overuse
* Failed project transitions

Retain recent run artifacts for debugging. A polished player-facing save system is not required.

---

# Final acceptance criteria

The vertical slice is complete when:

* Prompted and blank world generation both work.
* Unreal uses only approved authored assets.
* AI-generated data contains no raw Unreal asset paths.
* A generated town contains 20–30 simulated NPCs and approximately 12–18 enterable buildings.
* Every NPC has valid housing, activity, relationships, beliefs, and routines.
* The town continues operating while the director is disconnected.
* Generated maps differ materially in layout and world meaning.
* At least one AI-proposed location repurposing produces a coherent physical and social consequence.
* The inspection interface exposes the causal chain.
* Most test generations compile without manual repair.
* The project remains buildable at the end of every phase.

## Operating instructions for the handoff model

Work autonomously and make the smallest reasonable decision when details are missing. Record assumptions rather than stopping for ordinary ambiguity.

Keep the project compiling after each meaningful change. Test each subsystem before expanding it. Do not hide failed requirements behind placeholder implementations.

Use the full Unreal Editor toolset for development, inspection, asset wrapping, debugging, and testing. Preserve the schema/compiler boundary for actual generated worlds.

Do not expand scope until the core loop works:

```text
Generate world
→ Compile town
→ Simulate residents
→ Propose change
→ Validate change
→ Experience consequence
```
