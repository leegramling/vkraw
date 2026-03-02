#pragma once

#include "core/runtime/VkVisualizerApp.h"

namespace vkraw {

inline int runVkrawApp(int argc, char** argv) { return core::runtime::runVkrawApp(argc, argv); }
inline int runVkSceneApp(int argc, char** argv) { return core::runtime::runVkSceneApp(argc, argv); }

} // namespace vkraw
