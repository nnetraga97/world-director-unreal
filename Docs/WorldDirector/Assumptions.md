# World Director Assumptions

This file records explicit implementation assumptions made while executing `initial_plan.md`.

## 2026-08-21: asset-led theme

The final town theme will be selected after certifying the assets the user chose. Visual coherence, enterable interiors, runtime construction suitability, resident coverage, props, and landmarks take priority over the plan's provisional dark-fantasy label. Fab environments remain eligible; no vendor or art family is excluded before the audit.

## 2026-08-21: vendor assets and capability pack

Vendor assets remain in their original content roots and are not modified directly. Game-owned wrappers, metadata, catalogs, and runtime templates live under `Content/CapabilityPack/`. This reconciles the Phase 1A exit wording with Phase 1B's explicit instruction to wrap rather than modify vendor content.

## 2026-08-21: first commit after asset import

Art was imported before this repository had any commit or Git LFS configuration, so the plan's preferred sequencing cannot be reproduced historically. Because no asset has been committed, LFS is configured before the first `git add` and commit. This preserves the requirement's source-control intent without rewriting or discarding user-selected content.

## 2026-08-21: determinism boundary

Town simulation state transitions are deterministic for the same accepted world state and player/simulation inputs. Layout variation is seed-addressable for debugging, but navigation, PCG, physics, and rendering are not required to be bit-identical, matching Phase 3.

## 2026-08-22: runtime qualification outranks visual certification

A shell is eligible for procedural compilation only after the generated town
can navigate a short complete path across the actual spawned vendor-door
transform. Loading, rendering, collision primitives, and a named door component
are necessary but insufficient. Installed shells that fail this stricter gate
remain available for later remediation and cannot be selected meanwhile.

## 2026-08-22: semantic activity thresholds before furniture sockets

The installed vendor interiors do not provide stable named sockets for beds,
chairs, counters, and work surfaces, and their interior navigation can form
disconnected islands. Phase 4 therefore places invisible Smart Object stations
on connected exterior thresholds near each semantic entrance. This proves the
schedule, navigation, claim/release, contention, and belief-sharing loop without
claiming furniture-level interaction. Stable furniture sockets and interior
navigation remain a later content-hardening task.

## 2026-08-22: visual capture is evidence, not environment completion

The daylight capture confirms that the selected Fantastic Village buildings,
Quaternius residents, and sky belong to one Stylized Medieval Frontier family.
The capability Landscape still displays a checker material, and residents are
not legible in the current wide framing. Neither issue is treated as completed
environment polish.

## 2026-08-22: local CLI companion prerequisites

The first director provider is a bundled Python companion that invokes a
replaceable CLI command. The current macOS development environment supplies
`/usr/bin/python3`, an authenticated `codex` executable, and the project plugin
resources. Normal generation requires equivalent local prerequisites. The
companion checks `PATH`, common Homebrew and user binary locations, and standard
macOS Codex/ChatGPT application locations so Finder-launched Unreal sessions do
not depend on shell path initialization. The spawned agent receives the standard
package-manager runtime paths needed by script launchers such as Homebrew Codex.
It uses Codex's ephemeral read-only mode and ignores optional user config while
retaining authentication; deterministic fixture mode is test-only.

## 2026-08-22: layout-candidate validity boundary

Unreal produces opaque candidate IDs only after the accepted topology resolves
against the authored plot pattern, certified shells, route graph, and landmark
constraint without overlaps. Runtime navigation remains the final physical gate
after selection and compilation. A candidate summary never exposes transforms or
asset paths to the model.
