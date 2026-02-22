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

## 9. Startup Cache Warmup For An "Active" Display

To make `vkglobe` feel alive immediately at startup (instead of waiting for the first few frames of fetch/decode/compile), build a startup warmup pass around the initial camera `lat/lon/alt` before entering the main run loop.

Recommended startup sequence:

1. Create camera and globe state (same as today).
2. Compute startup sub-camera geodetic location and target zoom.
3. Build the visible tile window for that startup zoom/center.
4. Fetch/decode a bounded number of tiles synchronously or in a short warmup loop.
5. Build `GlobeTileLayer` nodes for those tiles.
6. Compile the tile layer scenegraph objects before the first frame (`viewer->compile()` or `compileManager->compile(...)` as appropriate).
7. Enter the run loop only after at least the center ring is ready.

This matches what `vsgLayt` does conceptually (`initial warmup` + incremental fill), but for globe tiles the key addition is compiling the tile textures and patch nodes before the first visible frame. In practice, a good startup target is "center tile + one or two rings" so the first image looks complete and responsive while outer rings continue to stream in.

Implementation note for this repo:

- Reuse the existing `OsmTileManager::update(...)` and `GlobeTileLayer::syncFromTileWindow(...)`.
- Add a startup warmup loop before `viewer->compile()` that runs `fetchAndDecodeBudgeted()` enough times to populate the startup window.
- If using runtime tile recompilation, compile the tile layer once before the first frame so descriptor/image uploads are already resident.

## 10. Panning Behavior On Globe

When user drags the globe, continuously update active tile set.

- Track current focus point under cursor (already available from globe hit test path).
- As focus tile changes, request neighbor window in movement direction first (priority queue).
- Keep previously visible ring for a short grace period to hide churn during fast pan.

## 11. Can We Replace Globe Mesh Textures Directly Instead Of Tile Patches?

Short answer: partially, but it is not the right long-term approach for close-up OSM imagery.

Two interpretations exist:

1. Replace the single full-globe texture with a newly composited image (global atlas-like update).
2. Reuse the same globe mesh but swap only local texture regions from cached OSM tiles.

Both are possible in principle, but both become awkward quickly:

- OSM tiles arrive in slippy tile coordinates (Web Mercator), not directly in the equirectangular UV layout used by the globe texture.
- You must resample/warp each OSM tile into the globe texture space.
- Updating one big texture every frame causes larger uploads and synchronization pressure than updating only visible patches.
- Resolution mismatch becomes severe: a single globe texture cannot preserve street-level detail globally.

Where this can still be useful:

- As a background "preview" layer at medium altitude.
- As a cached composite around the startup location to reduce patch count.
- As an offline bake for a static mission/region.

For your stated goals (tilt camera, close-up detail, roads/buildings later), per-tile globe patches remain the better design:

- natural LOD
- bounded uploads
- simple eviction
- direct mapping from OSM tile cache to render tile key

## 12. 3D Roads And Buildings At Close Altitude (LOD-Gated)

Once OSM imagery is stable, add vector/3D geometry only at low altitude so the GPU is not overloaded.

Recommended altitude bands:

- High altitude: imagery only (globe + raster tiles)
- Mid altitude: optional vector overlays (major roads, coastlines, labels)
- Low altitude: roads/buildings/water meshes for a small radius around the focus point

Key rules:

- Gate 3D generation by altitude threshold (for example below `5,000 ft`, then refine below `1,000 ft`).
- Build geometry in tile buckets keyed to the same `(z,x,y)` system as imagery for cache alignment.
- Use LOD by both distance and semantic importance:
  - roads: highways first, local streets later
  - buildings: simple extrusions first, detailed meshes only very near camera
- Keep a strict geometry budget per frame (creation/upload) and per scene (resident triangles).

GPU safety strategy:

- Use frustum culling and tile visibility tests before decoding/building geometry.
- Use parent-child LOD replacement (coarse region mesh remains until refined children are ready).
- Merge static geometry within a tile (roads/buildings) to reduce draw calls.
- Evict far tiles aggressively when altitude rises or focus moves.

This approach keeps the renderer scalable: raster OSM tiles provide continuous coverage, while heavier 3D content appears only where it is useful.

## 13. Integration Steps In This Repo

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

## 14. Provider, Policy, And Practical Notes

OpenStreetMap standard tile servers are not for heavy production traffic. For sustained usage:

- Respect OSM tile usage policy and user-agent requirements.
- Add cache-first behavior (already present in `vsgLayt` approach).
- Consider paid/self-hosted tile providers for high request rates.

## 15. Minimal Phase Plan

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
