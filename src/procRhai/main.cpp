#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

constexpr uint16_t CMD_SET_F32 = 1;
constexpr uint16_t CMD_SET_VEC3 = 2;
constexpr uint16_t CMD_SET_VEC4 = 3;
constexpr uint16_t CMD_DRAW_LINE = 4;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct TriangleState {
    Vec3 translation{};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct LineState {
    Vec3 p0{};
    Vec3 p1{};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct ModelState {
    Vec3 translation{};
    Vec3 rotation_euler{};
};

using animvm_create_fn = void* (*)();
using animvm_load_json5_fn = int (*)(void*, const char*);
using animvm_intern_path_fn = uint32_t (*)(void*, const char*);
using animvm_intern_param_fn = uint32_t (*)(void*, const char*);
using animvm_set_param_f32_fn = int (*)(void*, uint32_t, float);
using animvm_set_param_vec4_fn = int (*)(void*, uint32_t, float, float, float, float);
using animvm_tick_fn = int (*)(void*, float, float);
using animvm_get_commands_fn = int (*)(void*, const uint8_t**, size_t*);
using animvm_last_error_fn = const char* (*)(void*);
using animvm_destroy_fn = void (*)(void*);

struct Api {
    animvm_create_fn create = nullptr;
    animvm_load_json5_fn load_json5 = nullptr;
    animvm_intern_path_fn intern_path = nullptr;
    animvm_intern_param_fn intern_param = nullptr;
    animvm_set_param_f32_fn set_param_f32 = nullptr;
    animvm_set_param_vec4_fn set_param_vec4 = nullptr;
    animvm_tick_fn tick = nullptr;
    animvm_get_commands_fn get_commands = nullptr;
    animvm_last_error_fn last_error = nullptr;
    animvm_destroy_fn destroy = nullptr;
};

const char* kConfigJson5 = R"JSON5(
{
  bind: {
    model:    { type: "GltfModel", name: "robotA" },
    triangle: { type: "Triangle",  name: "tri1"   },
    line:     { type: "Line",      name: "line1"  }
  },

  params: {
    speed:  { type:"f32", default: 1.0, min:0.0, max:5.0 },
    height: { type:"f32", default: 1.25, min:0.0, max:3.0 },
    tint:   { type:"vec4", default:[1.0, 0.6, 0.1, 1.0] }
  },

  script: {
    lang: "rhai",
    code: `
      fn on_load() {
        // initial placement of objects
        set("triangle.transform.translation", vec3(-1.0, 0.0, 0.0));
        set("line.p0", vec3(-2.0, 0.0, 0.0));
        set("line.p1", vec3( 2.0, 0.0, 0.0));
        set("model.transform.translation", vec3(1.0, 0.0, 0.0));

        // initial colors
        set("triangle.color", param("tint"));
        set("line.color", vec4(0.2, 0.9, 1.0, 1.0));
      }

      fn on_frame(t, dt) {
        let speed  = param("speed");
        let height = param("height");

        // bounce curve (slow-in/slow-out)
        let phase = ((t * speed) / 1.2) % 1.0;
        let u = phase * phase * (3.0 - 2.0 * phase);
        let arc = 4.0 * u * (1.0 - u);
        let y = height * arc;

        // animate triangle up/down
        set("triangle.transform.translation", vec3(-1.0, y, 0.0));

        // animate line endpoint to track y
        set("line.p1", vec3(2.0, y, 0.0));

        // animate model bob + slight yaw
        set("model.transform.translation", vec3(1.0, 0.5 * y, 0.0));
        set("model.transform.rotation_euler", vec3(0.0, y, 0.0)); // MVP uses Euler; host can convert to quat later.

        // optional debug draw line
        draw_line(vec3(-1.0, y, 0.0), vec3(1.0, 0.5 * y, 0.0), vec4(1,1,1,1));
      }
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
    api.intern_path = reinterpret_cast<animvm_intern_path_fn>(sym("animvm_intern_path"));
    api.intern_param = reinterpret_cast<animvm_intern_param_fn>(sym("animvm_intern_param"));
    api.set_param_f32 = reinterpret_cast<animvm_set_param_f32_fn>(sym("animvm_set_param_f32"));
    api.set_param_vec4 = reinterpret_cast<animvm_set_param_vec4_fn>(sym("animvm_set_param_vec4"));
    api.tick = reinterpret_cast<animvm_tick_fn>(sym("animvm_tick"));
    api.get_commands = reinterpret_cast<animvm_get_commands_fn>(sym("animvm_get_commands"));
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
    api.intern_path = reinterpret_cast<animvm_intern_path_fn>(sym("animvm_intern_path"));
    api.intern_param = reinterpret_cast<animvm_intern_param_fn>(sym("animvm_intern_param"));
    api.set_param_f32 = reinterpret_cast<animvm_set_param_f32_fn>(sym("animvm_set_param_f32"));
    api.set_param_vec4 = reinterpret_cast<animvm_set_param_vec4_fn>(sym("animvm_set_param_vec4"));
    api.tick = reinterpret_cast<animvm_tick_fn>(sym("animvm_tick"));
    api.get_commands = reinterpret_cast<animvm_get_commands_fn>(sym("animvm_get_commands"));
    api.last_error = reinterpret_cast<animvm_last_error_fn>(sym("animvm_last_error"));
    api.destroy = reinterpret_cast<animvm_destroy_fn>(sym("animvm_destroy"));
#endif

    return api.create && api.load_json5 && api.intern_path && api.intern_param && api.set_param_f32 && api.set_param_vec4 && api.tick &&
           api.get_commands && api.last_error && api.destroy;
}

int failWithVmError(const char* step, void* vm, const Api& api)
{
    const char* err = (vm && api.last_error) ? api.last_error(vm) : nullptr;
    std::cerr << "error: " << step << " failed";
    if (err && std::strlen(err) > 0) std::cerr << ": " << err;
    std::cerr << '\n';
    return EXIT_FAILURE;
}

uint16_t read_u16_le(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32_le(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

float read_f32_le(const uint8_t* p)
{
    const uint32_t u = read_u32_le(p);
    float v = 0.0f;
    std::memcpy(&v, &u, sizeof(float));
    return v;
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

    std::unordered_map<uint32_t, std::string> pathNames;
    auto internPath = [&](const char* path) -> uint32_t {
        const uint32_t id = api.intern_path(vm, path);
        pathNames[id] = path;
        return id;
    };

    const uint32_t kTriangleTranslation = internPath("triangle.transform.translation");
    const uint32_t kTriangleColor = internPath("triangle.color");
    const uint32_t kLineP0 = internPath("line.p0");
    const uint32_t kLineP1 = internPath("line.p1");
    const uint32_t kLineColor = internPath("line.color");
    const uint32_t kModelTranslation = internPath("model.transform.translation");
    const uint32_t kModelRotationEuler = internPath("model.transform.rotation_euler");

    const uint32_t speedId = api.intern_param(vm, "speed");
    const uint32_t heightId = api.intern_param(vm, "height");
    const uint32_t tintId = api.intern_param(vm, "tint");

    if (speedId == 0 || heightId == 0 || tintId == 0) {
        api.destroy(vm);
        return failWithVmError("animvm_intern_param", vm, api);
    }

    if (api.set_param_f32(vm, speedId, 1.3f) != 0 || api.set_param_f32(vm, heightId, 1.5f) != 0 ||
        api.set_param_vec4(vm, tintId, 1.0f, 0.6f, 0.1f, 1.0f) != 0) {
        api.destroy(vm);
        return failWithVmError("animvm_set_param_*", vm, api);
    }

    TriangleState tri{};
    LineState line{};
    ModelState model{};

    constexpr float dt = 1.0f / 60.0f;
    float t = 0.0f;

    std::cout << std::fixed << std::setprecision(3);
    for (int frame = 0; frame < 120; ++frame) {
        if (api.tick(vm, t, dt) != 0) {
            api.destroy(vm);
            return failWithVmError("animvm_tick", vm, api);
        }

        const uint8_t* cmdData = nullptr;
        size_t cmdLen = 0;
        if (api.get_commands(vm, &cmdData, &cmdLen) != 0) {
            api.destroy(vm);
            return failWithVmError("animvm_get_commands", vm, api);
        }

        size_t offset = 0;
        int cmdCount = 0;
        while (offset + 4 <= cmdLen) {
            const uint16_t type = read_u16_le(cmdData + offset + 0);
            const uint16_t size = read_u16_le(cmdData + offset + 2);
            if (size < 4 || offset + size > cmdLen) {
                std::cerr << "error: malformed command buffer\n";
                api.destroy(vm);
                return EXIT_FAILURE;
            }

            const uint8_t* payload = cmdData + offset + 4;
            if (type == CMD_SET_F32 && size == 12) {
                const uint32_t pathId = read_u32_le(payload + 0);
                const float v = read_f32_le(payload + 4);
                (void)pathId;
                (void)v;
            } else if (type == CMD_SET_VEC3 && size == 20) {
                const uint32_t pathId = read_u32_le(payload + 0);
                const Vec3 v{read_f32_le(payload + 4), read_f32_le(payload + 8), read_f32_le(payload + 12)};
                if (pathId == kTriangleTranslation) tri.translation = v;
                else if (pathId == kLineP0) line.p0 = v;
                else if (pathId == kLineP1) line.p1 = v;
                else if (pathId == kModelTranslation) model.translation = v;
                else if (pathId == kModelRotationEuler) model.rotation_euler = v;
            } else if (type == CMD_SET_VEC4 && size == 24) {
                const uint32_t pathId = read_u32_le(payload + 0);
                const Vec4 v{read_f32_le(payload + 4), read_f32_le(payload + 8), read_f32_le(payload + 12), read_f32_le(payload + 16)};
                if (pathId == kTriangleColor) tri.color = v;
                else if (pathId == kLineColor) line.color = v;
            } else if (type == CMD_DRAW_LINE && size == 44) {
                // debug draw command consumed for count only in this MVP
            }

            ++cmdCount;
            offset += size;
        }

        std::cout << "frame=" << frame << " t=" << t << " tri.t=(" << tri.translation.x << "," << tri.translation.y << ","
                  << tri.translation.z << ") line.p1=(" << line.p1.x << "," << line.p1.y << "," << line.p1.z << ") model.t=("
                  << model.translation.x << "," << model.translation.y << "," << model.translation.z << ") model.rot=("
                  << model.rotation_euler.x << "," << model.rotation_euler.y << "," << model.rotation_euler.z << ") cmds=" << cmdCount
                  << '\n';

        t += dt;
    }

    api.destroy(vm);
    return EXIT_SUCCESS;
}
