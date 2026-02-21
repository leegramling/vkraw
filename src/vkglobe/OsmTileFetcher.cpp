#include "vkglobe/OsmTileFetcher.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace vkglobe {

namespace {
#if !defined(_WIN32)
std::string shellQuote(const std::string& value)
{
    std::string quoted = "'";
    for (char ch : value)
    {
        if (ch == '\'')
            quoted += "'\"'\"'";
        else
            quoted += ch;
    }
    quoted += "'";
    return quoted;
}
#endif
} // namespace

bool downloadOsmTileIfNeeded(int zoom, int x, int y, const std::filesystem::path& cacheFile)
{
    if (std::filesystem::exists(cacheFile)) return true;
    std::filesystem::create_directories(cacheFile.parent_path());

    const std::string url = "https://tile.openstreetmap.org/" + std::to_string(zoom) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png";
    std::ostringstream command;
#if defined(_WIN32)
    command << "curl -L --fail --silent --show-error "
            << "--connect-timeout 5 --max-time 20 "
            << "-A \"vkglobe/0.1 (tile prototype)\" "
            << "-o \"" << cacheFile.string() << "\" "
            << "\"" << url << "\"";
#else
    command << "curl -L --fail --silent --show-error "
            << "--connect-timeout 5 --max-time 20 "
            << "-A " << shellQuote("vkglobe/0.1 (tile prototype)") << " "
            << "-o " << shellQuote(cacheFile.string()) << " "
            << shellQuote(url);
#endif

    const int rc = std::system(command.str().c_str());
    if (rc != 0)
    {
        std::filesystem::remove(cacheFile);
        return false;
    }

    return true;
}

} // namespace vkglobe

