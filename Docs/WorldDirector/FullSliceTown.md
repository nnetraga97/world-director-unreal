# Full-Slice Town

Phase 6 raises the playable contract from the original eight-location compiler
fixture to a generated town containing 12–18 buildings and 20–30 autonomous
residents. The reference fixture deliberately uses the upper building bound:
18 locations, 24 residents, six households, 46 reciprocal directed
relationships, six closed facts, and one initial central threat.

## Asset-led visual family

The compiled town uses the installed Fantastic Village architecture and
compatible Quaternius modular residents as one Stylized Medieval Frontier
family. Procedural selection is limited to shells whose measured footprints,
doors, collision, and navigation passed the runtime qualification gate:

* compact homes;
* larger multiwing homes;
* public/workplace longhouses.

This produces three compatible architectural substyles without mixing in the
installed Old West, construction, realistic cabin, or sci-fi packs. Those
assets remain untouched and outside the procedural capability catalog.

## Full-slice validation

Before compilation, the integrated model output must contain:

* 12–18 locations with homes, workplaces, public space, and a landmark;
* 20–30 residents, each with a motivation, fear, memory, belief, and
  relationship;
* at least one initial central threat;
* one connected resident relationship graph.

Unreal builds several non-overlapping candidate layouts only after semantic
topology is accepted. The provider sees opaque candidate IDs and summaries,
never asset paths or transforms. Seeded layouts combine quarter-turn orientation
with shuffled location assignment inside physically certified home/public plot
pools; mapping signatures prevent duplicate candidates. The final 18-plot layout
has passed all actual door probes and resident route checks at 0, 90, 180, and
270 degrees.

Resident spawning is keyed per home and offset in the building's local entrance
frame. This supports generated households living in a public-purpose shell such
as a barracks or shelter; spawn collision may adjust placement, but the physical
navigation gate still rejects any inaccessible result.

## Resident interaction

Clicking a resident opens a compact native UMG dialogue menu showing their name,
occupation, motivation, fear, and known facts. The menu offers `Ask about what
they know`, `Offer help`, and `Leave`. It is deliberately a thin interaction
surface over canonical generated state, not a free-form runtime model call.

The headless interaction check reports
`WORLD_DIRECTOR_DIALOGUE_RESULT=PASS` only when the widget is in the viewport and
its resident content is built. macOS headless screenshot capture does not retain
UMG in this setup, so the test is runtime evidence rather than a claimed visual
screenshot proof.

## Acceptance evidence

The Phase 6 reference fixture passes:

* automation contract: 18 locations, 24 visually distinct modular resident
  combinations, 46 relationships, one connected social graph, one threat, and
  at least three shell substyles;
* four physical rotations: `WORLD_DIRECTOR_NAVIGATION_RESULT=PASS locations=18
  residents=24`;
* accelerated simulation: `days=3`, `residents=24`, `sharedBeliefs=46`,
  `relationshipEvents=96`, `closedFacts=6`;
* deterministic staged blank and prompted generation: 18 locations and 24
  residents each;
* real prompted run `20260822T105549Z-2105EB0B`: Brackenford, 18 locations, 20
  residents, 42 directed relationships, two threats, and physical PASS;
* real blank run `20260822T110043Z-8CAA6AC1`: Gloamwatch, 16 locations, 24
  residents, 48 directed relationships, and two threats. The unchanged accepted
  spec passed replay after the public-residence spawn fix.

The two real runs materially differ in identity, terrain, governance, tension,
supernatural premise, location count, population count, relationship count, and
compiler seed. The wide visual capture demonstrates a populated village layout,
but the capability landscape still has its checker material; environment polish
is therefore not part of this acceptance claim.
