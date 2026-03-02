#pragma once

#include "vkraw/GlobeObject.h"

#include <imgui.h>

namespace vkraw {

inline bool drawGlobeControlsPanel(GlobeObject& globe)
{
    ImGui::Begin("Globe Controls");
    ImGui::Text("LMB drag rotates globe (origin-anchored)");
    ImGui::Text("Arrow keys also rotate");
    ImGui::SliderFloat("Yaw", &globe.yaw, -180.0f, 180.0f);
    ImGui::SliderFloat("Pitch", &globe.pitch, -89.0f, 89.0f);
    ImGui::SliderFloat("Auto spin (deg/s)", &globe.autoSpinSpeedDeg, -180.0f, 180.0f);
    const bool changedLat = ImGui::SliderInt("Latitude segments", &globe.latitudeSegments, 32, 512);
    const bool changedLon = ImGui::SliderInt("Longitude segments", &globe.longitudeSegments, 64, 1024);
    const bool changedTileRows = ImGui::SliderInt("Tile rows", &globe.tileRows, 1, 32);
    const bool changedTileCols = ImGui::SliderInt("Tile cols", &globe.tileCols, 1, 64);
    const bool changedRadius = ImGui::SliderFloat("Radius", &globe.radius, 10.0f, 300.0f);
    ImGui::SliderFloat("Mouse rotate deg/pixel", &globe.mouseRotateDegreesPerPixel, 0.02f, 1.00f);
    ImGui::Text("Triangles %llu", static_cast<unsigned long long>(globe.triangles()));
    ImGui::Text("Vertices %llu", static_cast<unsigned long long>(globe.vertices()));
    ImGui::End();
    return changedLat || changedLon || changedTileRows || changedTileCols || changedRadius;
}

} // namespace vkraw

