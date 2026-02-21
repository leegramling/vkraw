# vkRaw (vk-bootstrap path)

This is the first renderer path for your comparison setup:
- Vulkan bootstrap/init: `vk-bootstrap`
- Window/input: `GLFW`
- Math: `GLM`
- UI: `ImGui` (demo window enabled)

Current app renders a 3D cube and opens an ImGui demo window.

## Submodules

Dependencies are added as git submodules under `external/`:
- `vk-bootstrap`
- `Vulkan-Headers`
- `glfw`
- `glm`
- `imgui`

Clone including submodules:

```bash
git clone --recurse-submodules <repo>
```

Or after clone:

```bash
git submodule update --init --recursive
```

## Windows 11 Build

Prereqs:
- Visual Studio 2022 (Desktop C++ workload)
- CMake 3.22+
- Vulkan SDK (for Vulkan loader + `glslc`/`glslangValidator`)

Build with Ninja:

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

Build with Visual Studio generator:

```bash
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
```

Run from the build directory so shader paths resolve:

```bash
cd build
./vkraw
```

## Controls

- Arrow keys: rotate cube
- ImGui window `Cube Controls`:
  - `Yaw`
  - `Pitch`
  - `Auto spin`
- ImGui demo window is shown each frame.

## Notes

- If no shader compiler is found, CMake still builds with placeholder `.spv` files for compile verification, but runtime rendering will fail.
- On a normal Windows Vulkan SDK install, real shader compilation should happen automatically.
