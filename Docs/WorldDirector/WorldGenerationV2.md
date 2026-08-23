# World Generation V2

World generation now treats terrain, settlement placement, routes, surfaces,
and biome dressing as replayable physical output. Narrative changes alone do
not make a new world.

## Runtime pipeline

```text
AI semantic brief
  -> certified environment profile
  -> terrain macroform and semantic surface field
  -> district anchors
  -> terrain-aware constrained plots
  -> connected curved road hierarchy
  -> exclusion-aware biome dressing
  -> V2 resolved physical recipe
  -> runtime procedural mesh, collision, assets, and navigation
```

The first enabled profile is `Profile.StylizedVillage`. Its game-owned Data
Asset is `/Game/WorldDirector/Profiles/DA_StylizedVillage`; it references the
installed Fantastic Village surface textures, water, rock, and dressing assets.
Downloaded realistic, western, construction, and science-fiction packs are not
cross-mixed into this profile.

## Determinism and persistence

`FWorldDirectorPhysicalGenerator` derives an independent SHA-256-based seed for
terrain, districts, plots, roads, surfaces, and dressing. Persisted identity
never uses `GetTypeHash` or `FCrc`. Component and world fingerprints are SHA-256
digests over explicitly ordered, integer-quantized physical data.

The same seed, semantic input, generator version, content version, and profile
reproduce the same resolved recipe and physical fingerprints. Rendering pixels,
physics tick ordering, and navmesh internal serialization remain outside that
contract; terrain samples, collision surface, transforms, routes, surface
classes, dressing recipe, and reachability gates are inside it.

Each successful AI run stores:

- `05-integrated-world.json`: accepted semantic V1 world state;
- `06-resolved-world-v2.json`: replayable physical recipe;
- `world-generation-lab.json`: candidate comparison and physical metrics;
- `run-summary.json`: AI telemetry plus physical fingerprints.

Legacy V1 artifacts did not persist physical output. They support semantic
inspection and re-resolution with their original generator only, not exact
physical replay. Known malformed legacy runs are quarantined in
`legacy-nonconforming-runs.json`; the original 14 schema files are frozen by
`legacy-v1-schema-manifest.json`. The reflected strict C++ reader is the V1
authority where old documentation schemas drifted.

## Uniqueness gate

The 50-seed automation corpus rejects duplicate world fingerprints and requires
adjacent seeds to differ on at least four physical axes: terrain samples,
surface field, districts, plots, routes, and dressing. Candidate IDs are derived
from full physical fingerprints, so rotating, renaming, or reshuffling the old
fixed slots can no longer manufacture a new candidate.

The diagnostics screen includes `Physical world` and `Candidates` views. These
show the exact recipe, terrain and surface statistics, route grades, component
fingerprints, selected candidate, and retained AI request/response telemetry.

## Runtime terrain substrate

The selected substrate is `UProceduralMeshComponent`, enabled explicitly by the
project and WorldDirector plugin. It produces a 65x65 quantized heightfield with
separate textured surface sections, synchronous collision cooking, a gravel
route ribbon mesh, optional water ribbon, and deterministic instanced dressing.
The authored Landscape is hidden and collision-disabled only while a generated
town is active, then restored during teardown.

Cooked-build collision, trace accuracy, and dynamic-navigation evidence remain
hard release gates. If Procedural Mesh cannot pass them, the recipe interface is
substrate-neutral and must be realized by certified modular terrain tiles; the
fixed Landscape is not an acceptable uniqueness fallback.
