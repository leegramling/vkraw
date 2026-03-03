#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using animvm_create_fn = void* (*)();
using animvm_load_json5_fn = int (*)(void*, const char*);
using animvm_tick_fn = int (*)(void*, float, float);
using animvm_get_ball_fn = int (*)(void*, float*, float*);
using animvm_last_error_fn = const char* (*)(void*);
using animvm_destroy_fn = void (*)(void*);

struct Api {
    animvm_create_fn create = nullptr;
    animvm_load_json5_fn load_json5 = nullptr;
    animvm_tick_fn tick = nullptr;
    animvm_get_ball_fn get_ball = nullptr;
    animvm_last_error_fn last_error = nullptr;
    animvm_destroy_fn destroy = nullptr;
};

const char* kConfigJson5 = R"JSON5(
{
  objects: {
    ball: {
      x: 0.0,
      y: 0.0,
      base_y: 0.0,
      height: 1.25,
      period: 1.2
    }
  },

  // Host provides: t (seconds), dt (seconds)
  script: {
    lang: "rhai",
    code: `
      // phase 0..1 repeating
      let phase = (t / ball.period) % 1.0;

      // smoothstep for slow-in/slow-out: u*u*(3-2u)
      let u = phase * phase * (3.0 - 2.0 * phase);

      // bounce arc 0->1->0: 4u(1-u)
      let arc = 4.0 * u * (1.0 - u);

      ball.y = ball.base_y + ball.height * arc;
    `
  }
}
)JSON5";

std::string platformLibraryName()
{
#ifdef _WIN32
    return "animvm.dll";
#elif __APPLE__
    return "libanimvm.dylib";
#else
    return "libanimvm.so";
#endif
}

std::string dirnameOf(const std::string& path)
{
    const size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    if (pos == 0) return path.substr(0, 1);
    return path.substr(0, pos);
}

std::vector<std::string> candidateLibraryPaths(int argc, char** argv)
{
    std::vector<std::string> paths;
    if (argc > 1) {
        paths.push_back(argv[1]);
        return paths;
    }

    const std::string libName = platformLibraryName();
    if (argc > 0 && argv && argv[0] && std::strlen(argv[0]) > 0) {
        paths.push_back(dirnameOf(argv[0]) + "/" + libName);
    }
    paths.push_back("./" + libName);
    paths.push_back(libName);
    return paths;
}

bool loadApi(const std::string& libraryPath, Api& api)
{
#ifdef _WIN32
    HMODULE lib = LoadLibraryA(libraryPath.c_str());
    if (!lib) {
        std::cerr << "error: LoadLibrary failed for " << libraryPath << '\n';
        return false;
    }
    auto sym = [&](const char* name) -> FARPROC { return GetProcAddress(lib, name); };
    api.create = reinterpret_cast<animvm_create_fn>(sym("animvm_create"));
    api.load_json5 = reinterpret_cast<animvm_load_json5_fn>(sym("animvm_load_json5"));
    api.tick = reinterpret_cast<animvm_tick_fn>(sym("animvm_tick"));
    api.get_ball = reinterpret_cast<animvm_get_ball_fn>(sym("animvm_get_ball"));
    api.last_error = reinterpret_cast<animvm_last_error_fn>(sym("animvm_last_error"));
    api.destroy = reinterpret_cast<animvm_destroy_fn>(sym("animvm_destroy"));
#else
    void* lib = dlopen(libraryPath.c_str(), RTLD_NOW);
    if (!lib) {
        std::cerr << "error: dlopen failed for " << libraryPath << ": " << dlerror() << '\n';
        return false;
    }
    auto sym = [&](const char* name) -> void* { return dlsym(lib, name); };
    api.create = reinterpret_cast<animvm_create_fn>(sym("animvm_create"));
    api.load_json5 = reinterpret_cast<animvm_load_json5_fn>(sym("animvm_load_json5"));
    api.tick = reinterpret_cast<animvm_tick_fn>(sym("animvm_tick"));
    api.get_ball = reinterpret_cast<animvm_get_ball_fn>(sym("animvm_get_ball"));
    api.last_error = reinterpret_cast<animvm_last_error_fn>(sym("animvm_last_error"));
    api.destroy = reinterpret_cast<animvm_destroy_fn>(sym("animvm_destroy"));
#endif

    return api.create && api.load_json5 && api.tick && api.get_ball && api.last_error && api.destroy;
}

int failWithVmError(const char* step, void* vm, const Api& api)
{
    const char* err = (vm && api.last_error) ? api.last_error(vm) : nullptr;
    std::cerr << "error: " << step << " failed";
    if (err && std::strlen(err) > 0) std::cerr << ": " << err;
    std::cerr << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv)
{
    Api api{};
    std::string loadedFrom;
    for (const auto& candidate : candidateLibraryPaths(argc, argv)) {
        if (loadApi(candidate, api)) {
            loadedFrom = candidate;
            break;
        }
    }
    if (loadedFrom.empty()) {
        std::cerr << "error: unable to resolve animvm symbols from any candidate path\n";
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Loaded animvm from " << loadedFrom << '\n';

    void* vm = api.create();
    if (!vm) return failWithVmError("animvm_create", vm, api);

    if (api.load_json5(vm, kConfigJson5) != 0) {
        api.destroy(vm);
        return failWithVmError("animvm_load_json5", vm, api);
    }

    constexpr float dt = 1.0f / 60.0f;
    float t = 0.0f;
    for (int frame = 0; frame < 240; ++frame) {
        if (api.tick(vm, t, dt) != 0) {
            api.destroy(vm);
            return failWithVmError("animvm_tick", vm, api);
        }
        float x = 0.0f;
        float y = 0.0f;
        if (api.get_ball(vm, &x, &y) != 0) {
            api.destroy(vm);
            return failWithVmError("animvm_get_ball", vm, api);
        }
        std::cout << "t=" << t << ", ball.x=" << x << ", ball.y=" << y << '\n';
        t += dt;
    }

    api.destroy(vm);
    return EXIT_SUCCESS;
}
