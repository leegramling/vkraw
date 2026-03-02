#include "core/AppRunner.h"

#include "core/runtime/VkVisualizerApp.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace core {

namespace {

void printHelp(const char* appName)
{
    std::cout << "Usage: " << appName << " [options]\n"
              << "Options:\n"
              << "  --help                    Show this help\n"
              << "  --seconds <value>         Run duration in seconds\n"
              << "  --duration <value>        Alias for --seconds\n"
              << "  --earth-texture <path>    Texture image path (.jpg/.jpeg/.png";
#if defined(VKRAW_ENABLE_IMAGE_FILE_IO)
    std::cout << "/.tif/.tiff";
#endif
    std::cout << ")\n";
}

} // namespace

int runVisualizerApp(int argc, char** argv, bool sceneMode, const char* appName)
{
    try {
        core::runtime::VkVisualizerApp visualizer;
        visualizer.setSceneMode(sceneMode);

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                printHelp(appName);
                return EXIT_SUCCESS;
            }
            if ((arg == "--seconds" || arg == "--duration") && (i + 1) < argc) {
                visualizer.setRunDurationSeconds(std::stof(argv[++i]));
            } else if (arg == "--earth-texture" && (i + 1) < argc) {
                visualizer.setEarthTexturePath(argv[++i]);
            }
        }

        visualizer.run();
    } catch (const std::exception& e) {
        std::cout << "[EXIT] " << appName << " status=FAIL code=1 reason=\"" << e.what() << "\"" << std::endl;
        std::cerr << "error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

} // namespace core
