# vkcornell

`vkcornell` is a minimal Cornell-box raster app built on top of `VulkanCore` and `EngineCore`.

Current goals:
- Create Cornell box geometry as `EngineCore::Model` data.
- Render it with a simple color pipeline through `VulkanCore`.
- Keep mesh/buffer flow compatible with later ray-tracing work.

Notes:
- This target is wired into `The-Modern-Vulkan-Cookbook/CMakeLists.txt` under `WIN32` (matching the existing chapter setup and surface creation path in `VulkanCore::Context`).
