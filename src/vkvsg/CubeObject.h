#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <vsg/all.h>

namespace vkvsg {

class CubeObject {
public:
    float yaw = 30.0f;
    float pitch = 20.0f;
    float autoSpinDegPerSec = 22.5f;
    int cubeCount = 100000;

    uint64_t triangles() const { return static_cast<uint64_t>(cubeCount) * 12ULL; }
    uint64_t vertices() const { return static_cast<uint64_t>(cubeCount) * 8ULL; }

    void applyInput(bool left, bool right, bool up, bool down, float dt)
    {
        constexpr float rotationSpeed = 90.0f;
        if (left) yaw -= rotationSpeed * dt;
        if (right) yaw += rotationSpeed * dt;
        if (up) pitch += rotationSpeed * dt;
        if (down) pitch -= rotationSpeed * dt;
    }

    vsg::dmat4 computeRotation(float elapsedSeconds) const
    {
        const double yawRadians = vsg::radians(static_cast<double>(yaw + autoSpinDegPerSec * elapsedSeconds));
        const double pitchRadians = vsg::radians(static_cast<double>(pitch));
        return vsg::rotate(yawRadians, 0.0, 0.0, 1.0) * vsg::rotate(pitchRadians, 1.0, 0.0, 0.0);
    }
};

} // namespace vkvsg
