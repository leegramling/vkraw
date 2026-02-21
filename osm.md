# OSM Tile Streaming Plan For `vkvsg`

This document explains how `../vsgLayt` currently handles OpenStreetMap (OSM) tiles, and how to apply the same pattern to `vkvsg` so detailed map textures stream onto globe tiles as the camera pans and zooms.

## 1. What `vsgLayt` Already Does Well

`vsgLayt` uses a clean slippy-map pipeline that is directly reusable.

- Projection math is Web Mercator in pixel space (`lon/lat <-> pixel`, zoom-based world size) in `../vsgLayt/src/Projection.cpp`.
- Tile state and cache tracking are centralized in `MapState` (`TileKey`, `TileEntry`, cache map, dirty flags) in `../vsgLayt/src/MapState.cpp`.
- Tile download uses URL pattern `https://tile.openstreetmap.org/{z}/{x}/{y}.png`, disk cache, and retry-safe fetch behavior in `../vsgLayt/src/TileFetcher.cpp`.
- Loading is incremental per frame (`kTileLoadBudgetPerFrame`) plus startup warmup (`kInitialTileWarmup`) from `../vsgLayt/include/vmap/Constants.h`.
- New textures are marked `pendingCompile`, then GPU compilation is triggered in the frame loop and cleared once done (`../vsgLayt/src/main.cpp`, `../vsgLayt/src/MapUiCommand.cpp`).

The two most important behaviors to preserve in `vkvsg` are:

1. Decouple network/disk loading from draw traversal.
2. Compile newly arrived textures in controlled batches, not all at once.

## 2. Why Globe Integration Is Different

`vsgLayt` is a flat 2D map canvas. `vkvsg` is an ellipsoid globe with rotation and camera distance changes.

So we need to add two globe-specific pieces:

- A method to determine the geodetic area currently visible on the globe.
- A globe mesh organization that supports per-tile textures instead of one global texture image.

`vkvsg` already has useful building blocks:

- Ray casting and ellipsoid intersection (`computeRayFromPointer`, `intersectEllipsoid`) in `src/vkvsg/VsgVisualizer.cpp`.
- Ellipsoid model and camera setup already in feet.
- Existing input loop where we can run tile update logic each frame.

## 3. Proposed `vkvsg` OSM Architecture

Add a dedicated globe tile subsystem:

- `OsmTileManager`
- `OsmProjection` (Mercator helpers, copied/adapted from `vsgLayt`)
- `OsmTileFetcher` (same cache+download flow)
- `GlobeTileLayer` (builds/owns VSG nodes for currently active tiles)

Core data structures:

- `TileKey { int z, x, y }`
- `TileEntry { image, texture/sampler state, node, compiled, lastUsedFrame, status }`
- `ActiveSet` and `RequestedSet` each frame

Recommended cache paths:

- Disk: `cache/osm/{z}/{x}/{y}.png` (same pattern as `vsgLayt`)
- Memory: LRU by tile count and memory budget

## 4. Activation By Altitude (500 ft Target)

Use a hysteresis gate to avoid mode thrash.

- Enable OSM tile layer when camera altitude above terrain/ellipsoid is below `800 ft`.
- Disable OSM tile layer when altitude rises above `1200 ft`.

This gives your requested ~`500 ft` behavior while keeping transitions stable. Inside this range, target zoom should move toward high OSM zoom levels (`z=18..19`, optionally `20` if provider permits).

Altitude calculation:

1. Use camera eye in ECEF/world.
2. Intersect ray toward globe center with ellipsoid.
3. `altitude_ft = length(eye - surfacePoint)`.

## 5. Choosing Which Tiles To Load

At each frame:

1. Find the camera focus geodetic point (lat/lon) using existing ray/ellipsoid intersection.
2. Convert focus lat/lon to slippy tile index for selected zoom.
3. Estimate visible angular footprint from camera FOV + altitude.
4. Convert footprint to tile radius `(rx, ry)` around center tile.
5. Request all tiles in that window.

Use Web Mercator tile math from `vsgLayt`:

- `n = 2^z`
- `x = floor((lon + 180) / 360 * n)`
- `y = floor((1 - asinh(tan(lat))/pi)/2 * n)`

Then clamp/wrap exactly as `vsgLayt` does:

- Wrap `x` modulo tile count.
- Clamp `y` to `[0, n-1]`.

## 6. Mapping OSM Tiles Onto Globe Geometry

Do not keep one monolithic globe mesh once OSM mode is enabled. Build a quadtree-like globe tile mesh.

Recommended geometry approach:

- One renderable patch per tile key `(z,x,y)`.
- Patch corners computed from tile lat/lon bounds projected to ellipsoid ECEF.
- Tessellate each patch locally (for curvature) with shared edge rules.
- UV for each patch is local `[0..1]` so each OSM tile texture maps directly.

This avoids atlas complexity and makes cache eviction/replacement straightforward.

## 7. Seams And Visual Continuity

To avoid cracks and visible borders:

- Enforce identical edge vertex generation for neighboring tiles.
- Keep same tessellation pattern on shared boundaries.
- Use a tiny “skirt” or edge overlap only if precision artifacts remain.
- Keep parent tile visible until all child tiles at higher zoom are ready.

For texture seams:

- Use `CLAMP_TO_EDGE` sampler mode for per-tile textures.
- Keep consistent color space handling (`sRGB` path) across all tiles.

## 8. Loading, Compilation, And Frame Budget

Reuse `vsgLayt`’s staged pipeline:

- Network/disk fetch in worker tasks.
- Decode to `vsg::Data` off main render traversal.
- Mark entries as `pendingCompile`.
- Compile in small batches during frame loop.

Suggested limits:

- `maxFetchPerFrame = 4`
- `maxCompilePerFrame = 4`
- startup warmup around view center: `32` tiles

This prevents frame spikes while panning.

## 9. Panning Behavior On Globe

When user drags the globe, continuously update active tile set.

- Track current focus point under cursor (already available from globe hit test path).
- As focus tile changes, request neighbor window in movement direction first (priority queue).
- Keep previously visible ring for a short grace period to hide churn during fast pan.

## 10. Integration Steps In This Repo

1. Add new files under `src/vkvsg/`:
- `OsmProjection.h/.cpp`
- `OsmTileFetcher.h/.cpp`
- `OsmTileManager.h/.cpp`
- `GlobeTileLayer.h/.cpp`

2. In `src/vkvsg/VsgVisualizer.cpp`:
- Add CLI flags:
  - `--osm`
  - `--osm-cache <path>`
  - `--osm-enable-alt-ft <value>` (default `800`)
  - `--osm-disable-alt-ft <value>` (default `1200`)
  - `--osm-max-zoom <value>` (default `19`)
- Create `OsmTileManager` during startup.
- In frame loop, run:
  - camera altitude update
  - active tile selection
  - incremental fetch/compile
  - scene patch add/remove

3. Keep existing single-texture globe path as fallback when OSM disabled or unavailable.

## 11. Provider, Policy, And Practical Notes

OpenStreetMap standard tile servers are not for heavy production traffic. For sustained usage:

- Respect OSM tile usage policy and user-agent requirements.
- Add cache-first behavior (already present in `vsgLayt` approach).
- Consider paid/self-hosted tile providers for high request rates.

## 12. Minimal Phase Plan

Phase 1:

- Implement `OsmProjection + OsmTileFetcher + OsmTileManager`.
- Log selected tiles per frame; no rendering yet.

Phase 2:

- Render one patch per tile with downloaded texture.
- Enable below altitude threshold.

Phase 3:

- Add parent/child LOD blending, eviction policy, and pan-prioritized queue.

Phase 4:

- Optional vector overlays (roads/buildings/water) using the same tile key space.

