# World Director Capability Pack

Audit date: 2026-08-22
Plan phase: 1A/1B
Theme: **Stylized Medieval Frontier**

This document is the human-readable companion to
`Saved/WorldDirector/asset-audit.json`. Every asset listed as certified below
was loaded or opened in Unreal Editor 5.8 and inspected. Filename-only
inferences are not treated as certification.

## Recorded assumptions

### P1-ASSET-ROOT-001 — Preserve vendor roots

Vendor assets remain in their imported roots (`/Game/Fantastic_Village_Pack`
and `/Game/Vendor/Quaternius`). Game-owned maps, shell wrappers, and interior
templates live in `/Game/CapabilityPack`. This satisfies the plan's vendor
isolation rule without duplicating hundreds of binary assets.

### P1-THEME-001 — Let the installed assets determine the setting

The installed Fab **Fantastic Village Pack** is the architectural anchor. Its
timber-and-plaster silhouettes, purple roofs, and low-detail materials support
a warm stylized medieval frontier better than the original dark-fantasy idea.
Quaternius modular characters and furniture were retained only after visual
inspection showed that their proportions and stylization are compatible.

The realistic `Fishermans_Cabin` and `Modular_Rural_Cabin` packs, the Old West
prop collection, the construction pack, and the sci-fi trooper are excluded
from the active town palette. They remain vendor content, not certified World
Director capabilities.

### P1-ANIMATION-001 — Use the bundled rigs and clips

The vertical slice uses the Quaternius male and female skeletons and their
bundled 24-clip animation sets. It does not retarget Manny animations in this
slice. This keeps locomotion and interaction deterministic while preserving a
future IK-retargeting option.

## Terrain envelope

| Capability | Certified asset | Evidence |
|---|---|---|
| Town terrain | `/Game/CapabilityPack/Maps/L_CapabilityLandscape` | Authored Landscape, 2,016 × 2,016 quads at 100 cm spacing; measured bounds `[-100800, 100800]` cm on X and Y (2.016 km square) |
| Buildable basin | Landscape center | Vertical trace at `(0, 0)` hit Z `0` cm; the central radius is intentionally flat |
| Landmark shelf | Same Landscape | Vertical trace near `(58464, 42336)` hit Z `2116.7` cm; the shelf is visible from basin level |
| Edge variation | Same Landscape | Sampled edge heights range from roughly `47.7` to `354.7` cm with overall measured terrain Z bounds `-264.1` to `2118.0` cm |

The map includes the complete audit lighting set: movable 100,000-lux
Directional Light, Sky Atmosphere, subtle Exponential Height Fog, default
Volumetric Cloud material, real-time Sky Light, and an unbound neutral-exposure
Post Process Volume. `r.EyeAdaptation.CachedLightingPreExposure=8` covers the
physically based exposure range without the UE 5.8 cached-lighting warning.

## Building shells

All shell capabilities are game-owned child Blueprints. Their parents are Fab
Blueprints; no vendor Blueprint or mesh was edited. Each inspected parent has
a separate body mesh and front-door component. The referenced body meshes use
materials and blocking simple/complex collision; representative body
`SM_BLD_body_v01_01` reports nine simple collision primitives.

| Wrapper | Parent | Archetype / substyle | Measured bounds (cm) | Capacity | Compatible interior |
|---|---|---|---:|---:|---|
| `/Game/CapabilityPack/Buildings/BP_Cap_Home_Tall_01` | `BP_BLD_house_1` | Home / vertical timber | 821 × 864 × 1847 | 2–3 | Small home template |
| `/Game/CapabilityPack/Buildings/BP_Cap_Home_Compact_01` | `BP_BLD_house_2` | Home / compact timber | 791 × 864 × 990 | 1–2 | Small home template |
| `/Game/CapabilityPack/Buildings/BP_Cap_Home_Compact_02` | `BP_BLD_house_3` | Home / broad compact | 968 × 1346 × 1518 | 2–4 | Small or roomy home template |
| `/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Longhouse_01` | `BP_BLD_house_4` | Workplace / longhouse | 1330 × 1783 × 1210 | 4–8 | Workplace template |
| `/Game/CapabilityPack/Buildings/BP_Cap_Home_Multiwing_01` | `BP_BLD_house_5` | Home / multiwing | 1735 × 1239 × 1390 | 4–6 | Roomy home template |
| `/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Inn_01` | `BP_BLD_house_6` | Workplace / compact inn | 722 × 912 × 1170 | 4–6 | Small home template or prop-only inn dressing |
| `/Game/CapabilityPack/Buildings/BP_Cap_Home_Tall_02` | `BP_BLD_house_7` | Home / tall compound | 1410 × 1928 × 1454 | 4–6 | Roomy home template |
| `/Game/CapabilityPack/Buildings/BP_Cap_Workplace_Guildhall_01` | `BP_BLD_house_8` | Workplace / civic hall | 1238 × 1633 × 1528 | 6–10 | Workplace template |

The eight wrappers were compiled with warnings treated as errors and placed
together in `/Game/CapabilityPack/Maps/L_CapabilityAudit`. The lineup confirms
two clear substyles—compact/tall homes and long/civic workplace compounds—while
remaining one coherent Fantastic Village architectural family. Wrapper
metadata includes semantic archetype, style, capacity, purposes, supported
states, front-door entrance policy, five-degree slope tolerance, and dynamic
navigation/door-clearance requirements.

## Interior templates and entrances

| Template | Room / opening | Certified contents | Compatible shell class |
|---|---:|---|---|
| `/Game/CapabilityPack/Interiors/BP_Interior_Home_Small_01` | 700 × 700 × 360 cm; 160 cm entrance | Twin bed, table, chair, bookcase | Compact and tall homes; compact inn dressing |
| `/Game/CapabilityPack/Interiors/BP_Interior_Home_Compact_01` | 900 × 800 × 380 cm; 200 cm entrance | Double bed, table, chair, bookcase | Broad compact, multiwing, and tall-compound homes |
| `/Game/CapabilityPack/Interiors/BP_Interior_Workplace_Longhouse_01` | 1200 × 900 × 380 cm; 200 cm entrance | Service counter, work table, two chairs, stool, bookcase, partition | Longhouse and guildhall |

Each template has a collision-backed floor, four enclosing wall runs, and a
measured front opening. Furniture was placed using its Unreal-reported local
bounds, not visual guesswork. All three templates compile with warnings as
errors, carry `WorldDirector.InteriorTemplate`, persist after a map reload, and
are visually confirmed in `L_CapabilityAudit` without prop/wall overlap.

The shell wrapper's separate front-door component is the authoritative exterior
entrance. During town compilation, a compatible interior template is aligned
behind that entrance; the compiler must reject a shell/interior pairing whose
declared footprint or entrance clearance does not fit.

### Phase 3 runtime qualification

The Phase 1 audit established that all eight wrappers load, render, expose door
components, and have collision-backed geometry. Phase 3 added a stricter test:
dynamic navigation must produce a short complete path across both sides of the
actual spawned vendor-door transform. Under that test, only
`BP_Cap_Home_Compact_01`, `BP_Cap_Home_Multiwing_01`, and
`BP_Cap_Workplace_Longhouse_01` currently qualify for procedural selection.

`BP_Cap_Home_Compact_02`, `BP_Cap_Home_Tall_02`,
`BP_Cap_Workplace_Inn_01`, and `BP_Cap_Workplace_Guildhall_01` produced
disconnected door-side nav islands in the compiled fixture. They remain valid
installed art but are excluded from compiler selection pending collision or
entrance remediation. The other visually audited wrapper is likewise not
promoted until it passes the same runtime gate. This qualification supersedes
the broader Phase 1 assumption that a visible door component alone proved
runtime enterability.

## Furniture, household capacity, and collision

Twenty Quaternius furniture FBXs were imported as Static Mesh assets under
`/Game/Vendor/Quaternius/Furniture`. A four-hull convex collision pass completed
for all twenty with zero failures, and all assets were saved with assigned
materials. The following assets are the certified semantic anchors:

| Semantic capability | Asset(s) |
|---|---|
| Beds | `BedTwin`, `BedDouble` |
| Work surfaces | `SM_Furniture_Table`, `SM_Furniture_Table2` |
| Chairs | `Chair`, `SM_Furniture_Stool` |
| Counter / service surface | `Desk` |
| Storage / dressing | `Bookcase`, `Bookcase_Books`, `Closet`, `ShortCloset`, `NightStand` |

Reported bounds used in the templates include: `BedTwin` 206 × 426 × 156 cm,
`BedDouble` 283 × 426 × 156 cm, table 142 × 278 × 82 cm, chair 51 × 63 ×
107 cm, desk 182 × 85 × 92 cm, and bookcase 184 × 66 × 337 cm.

## Modular residents and occupation visuals

| Family | Root / skeleton | Certified modular meshes | Animation clips | Skeleton audit |
|---|---|---:|---:|---|
| Male | `/Game/Vendor/Quaternius/Characters/Male/Farmer_Body` / `Farmer_Body_Skeleton` | 17 across Farmer, Adventurer, King, and Worker parts | 24 | All share one 80-bone skeleton; nonzero vertices and assigned materials |
| Female | `/Game/Vendor/Quaternius/Characters/Female/Medieval_Body` / `Medieval_Body_Skeleton` | 17 across Medieval, Adventurer, Worker, and Witch parts | 24 | All share one 63-bone skeleton; nonzero vertices and assigned materials |

A conservative curated resident palette uses four coherent outfit families per
skeleton and four compatible material/hair variants per family: 16 male plus
16 female appearances, or 32 visually distinct residents without arbitrary
cross-family part mixing. That clears the 20–30 resident requirement.

Occupation-read mappings for the slice:

| Occupation read | Certified parts |
|---|---|
| Farmer / laborer / carpenter | Farmer and Worker sets |
| Merchant / innkeeper | Medieval or Farmer body with Worker vest/material variants |
| Guard / scout | Adventurer set and backpack |
| Reeve / civic official | King set or formal Medieval set |
| Herbalist / unusual specialist | Witch set |

These are visual reads, not job logic. Occupations, schedules, and equipment
rules are implemented in later phases.

## Landmark types

No unrelated ruins pack was forced into the theme. The certified landmark
strategy uses the same architectural family:

* `BP_Cap_Workplace_Guildhall_01` as a civic landmark on the elevated shelf.
* `BP_Cap_Home_Tall_01` or `BP_Cap_Home_Tall_02` as a watch-house / tower
  silhouette.
* The Landscape's elevated shelf as the placement envelope.

This preserves regional coherence and meets the slice's need for at least one
recognizable landmark type.

## Location states and conversions

The wrappers declare compatibility with semantic states `Active`, `Worn`,
`Damaged`, `Abandoned`, and `Converted`. Phase 1 certifies geometry capable of
receiving those states; it does **not** claim that state-specific material or
prop overlays already exist.

Conversions supported by the current physical inventory:

| From | To | Physical capability |
|---|---|---|
| Residence | Temporary clinic | Bed(s), work surface, chairs, storage; swap to a compatible home dressing set |
| Closed workshop | Active workshop | Workplace interior, counter, work table, seating, storage |
| Inn / public room | Shelter | Compact shell plus bed and seating anchors |
| Unused room | Faction headquarters | Workplace interior, service counter, table, chairs, bookcase |

Cellar/dungeon conversions are intentionally not certified; no compatible
dungeon kit was imported. Phase 7 implements the certified home/clinic,
workplace/shelter, shelter/headquarters, and headquarters/workplace paths and
independently refuses other source-to-purpose pairs.

## Smart Object opportunities

Phase 1 certifies where later Smart Objects can be attached:

* `Sleep` on `BedTwin` and `BedDouble`
* `Sit` on `Chair` and `SM_Furniture_Stool`
* `Work` on both tables and `Desk`
* `Serve/Trade` at `Desk`
* `Enter/Exit` at each wrapper's front-door component
* `Store/Inspect` at bookcases and closets

Phase 4 authors gameplay-tagged runtime Smart Object definitions and the
resident StateTree loop. The current activity stations sit at connected
semantic entrance thresholds because the visible vendor furniture lacks stable
socket names and the interior navmesh is not yet consistently connected. See
`LivingTownSimulation.md`; furniture-level interaction remains explicitly
uncertified.

## Phase 1 hard-gate result

**PASS.** The capability pack now has an authored 2.016 km Landscape, eight
distinct shell wrappers across multiple coherent substyles, three working
interior templates with entrance clearance, a certified 32-combination character
capacity whose compiler test demonstrates 24 visually distinct residents in the
full-slice fixture, and collision/material-certified furniture covering beds, work
surfaces, chairs, and counters. The catalog is sufficient to compile a small
test settlement with enterable homes and workplaces without editing vendor
content.
