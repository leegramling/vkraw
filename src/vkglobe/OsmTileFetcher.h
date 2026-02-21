#pragma once

#include <filesystem>

namespace vkglobe {

bool downloadOsmTileIfNeeded(int zoom, int x, int y, const std::filesystem::path& cacheFile);

} // namespace vkglobe

