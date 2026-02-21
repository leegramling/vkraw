# vkglobe TODO

This plan is derived from `osm.md` and is organized by priority.

## High Priority

- [ ] Create `OsmProjection` module in `src/vkglobe/` using Web Mercator tile math from `vsgLayt` (`lon/lat <-> pixel`, `z/x/y` conversions, x-wrap, y-clamp).
- [ ] Create `OsmTileFetcher` module with disk cache layout `cache/osm/{z}/{x}/{y}.png` and fetch path compatible with OSM tile URLs.
- [ ] Create `OsmTileManager` core state (`TileKey`, `TileEntry`, active/requested sets, pending compile state).
- [ ] Add `--osm` runtime flag in `vkglobe` to enable OSM mode while preserving current single-texture globe fallback.
- [ ] Add altitude-gated activation with hysteresis (initial defaults from `osm.md`: enable below 800 ft, disable above 1200 ft).
- [ ] Implement per-frame visible tile selection around camera focus point (lat/lon under cursor / camera center).
- [ ] Add incremental loading budgets (`maxFetchPerFrame`, `maxCompilePerFrame`) to avoid frame spikes during pan/zoom.
- [ ] Build globe tile patches with per-tile textures (replace monolithic texture path when OSM mode is active).
- [ ] Integrate startup tile warmup around current focus to reduce first-pan blank tiles.

## Medium Priority

- [ ] Add tile LOD policy (zoom selection from altitude + FOV + screen-space coverage).
- [ ] Keep parent tile visible until higher-zoom children are ready to prevent holes.
- [ ] Add seam controls across tile boundaries (shared edge sampling / optional skirts).
- [ ] Add memory LRU eviction policy by tile count + memory budget.
- [ ] Add request prioritization in pan direction (front-of-motion tiles first).
- [ ] Add robust error and telemetry logs (tile fetch fail rate, decode failures, compile backlog).
- [ ] Add CLI controls:
  - [ ] `--osm-cache <path>`
  - [ ] `--osm-enable-alt-ft <value>`
  - [ ] `--osm-disable-alt-ft <value>`
  - [ ] `--osm-max-zoom <value>`

## Low Priority

- [ ] Add optional vector overlay pipeline (roads/buildings/water) keyed to same tile IDs.
- [ ] Add optional blend/fade transitions between parent/child tiles.
- [ ] Add prefetch ring around viewport for smoother high-speed camera motion.
- [ ] Add alternate tile provider support abstraction (self-hosted or commercial provider).
- [ ] Add diagnostics UI panel for tile cache and OSM state (active tiles, cached tiles, pending compile, fetch queue depth).
- [ ] Add automated smoke test path (headless logic tests for tile addressing and cache state transitions).

## First Goal Set (Immediate)

1. `OsmProjection`
2. `OsmTileFetcher`
3. `OsmTileManager`
4. `--osm` + altitude gate
5. per-frame tile selection + budgeted fetch/compile
6. render per-tile textured globe patches

