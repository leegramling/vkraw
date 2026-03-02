# vkglobe TODO

This plan is derived from `osm.md` and is organized by priority.

## Cross-Project TODO (`src/core` extraction)

Goal: stop coupling new apps to `src/vkraw/*` directly and move reusable Vulkan/app scaffolding into `src/core/`.

- [x] Create `src/core/` module and move shared renderer/app abstractions there.
- [x] Define stable interfaces for:
  - [x] Vulkan context/device/swapchain lifecycle
  - [x] Buffer/image/upload helpers
  - [x] Pipeline creation (including topology selection)
  - [x] Descriptor set layout/pool allocation helpers
  - [x] Frame loop hooks (`processInput`, `update`, `record`, `draw`)
- [x] Move shared scene abstractions into core:
  - [x] `SceneGraph`
  - [x] `EcsWorld`
  - [x] `RenderObject` base
  - [x] scene draw-item representation
- [x] Move shared UI shell (ImGui init + main menu hooks) into core.
- [x] Add `vkScene` to use only `src/core` (no includes from `src/vkraw/*`).
- [x] Keep `vkraw` as a feature app built on `src/core` (globe-specific logic only in `src/vkraw`).
- [x] Add bindless descriptor path in core for per-object texture/material indexing.
- [x] Add per-object uniform management in core (allocation/update model).
- [x] Add named texture-slot registry in core runtime (`earth`, `checker`, and object material slot binding).
- [ ] Add documentation:
  - [ ] `core.md` architecture and ownership boundaries
  - [ ] migration checklist for adding new apps (`vkScene`, future apps)

## Documentation TODO (Natural Docs + Mermaid)

Goal: use Natural Docs for code documentation and include Mermaid diagrams in generated HTML documentation.

- [ ] Evaluate and lock Natural Docs version/tooling for this repo.
- [x] Add Natural Docs config and build command(s) to the repo.
  - [x] Add `docs/naturaldocs/` project config
  - [x] Add `docs/build_docs.sh` and Windows equivalent
  - [x] Add CMake custom target `docs`
- [ ] Define project doc style conventions:
  - [ ] file/class/function comment templates
  - [ ] Mermaid block formatting conventions in comments/docs
  - [ ] naming/placement conventions for architecture diagrams
- [ ] Validate Mermaid rendering in generated HTML output with at least one sample diagram.
  - [ ] Add one embedded Mermaid block in a C++ file comment and verify output
- [ ] Add CI or local check target to regenerate docs and validate build success.
- [ ] After `src/core` refactor is complete, add/update documentation comments across all source files.
- [ ] Add architecture docs with Mermaid diagrams for:
  - [ ] `src/core` runtime + Vulkan setup flow
  - [ ] `vkraw` app-specific globe layer
  - [ ] `vkScene` object/scene/material flow

## Workflow Rule

- [ ] Always run `git add` and commit after each completed task.

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
