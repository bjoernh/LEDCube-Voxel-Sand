#include "WaterCube.h"

#include <cstdint>
#include <iostream>

// ──────────────────────────────────────────────────────────────────────────

static inline Color unpackColor(uint32_t c) noexcept {
    return Color(
        static_cast<uint8_t>((c >> 16) & 0xFFu),
        static_cast<uint8_t>((c >>  8) & 0xFFu),
        static_cast<uint8_t>( c        & 0xFFu));
}

// ──────────────────────────────────────────────────────────────────────────

WaterCube::WaterCube(std::string serverUri)
    : CubeApplication(60, serverUri, "WaterCube")
    , engine_()
    , imu_()
{
    std::cout << "WaterCube initialised. Connecting to: " << serverUri << std::endl;

    params.registerFloat("gravityMagnitude", "Gravity (cells/s²)",  0.0f, 60.0f,
                         25.0f, 1.0f, "Physics");
    params.registerFloat("flipBlend",        "FLIP blend (0=PIC)",  0.0f,  1.0f,
                         0.95f, 0.05f, "Physics");
    params.registerInt  ("jacobiIterations", "Pressure iterations", 5, 80,
                         30, "Physics");
    params.registerFloat("fillLevel",        "Fill level",          0.0f,  1.0f,
                         0.40f, 0.05f, "Fluid");
    params.registerEnum ("refill",           "Refill",
                         {"idle", "refill"}, "idle", "Fluid");
    params.registerEnum ("orientation",      "Gravity source",
                         {"IMU", "Keyboard"}, "IMU", "Control");
}

// ──────────────────────────────────────────────────────────────────────────

OrientationSource& WaterCube::activeSource() {
    const std::string mode = params.getString("orientation");
    if (mode == "Keyboard") {
        if (!keyboard_) keyboard_ = std::make_unique<KeyboardTilt>();
        return *keyboard_;
    }
    return imu_;
}

// ──────────────────────────────────────────────────────────────────────────

bool WaterCube::loop() {
    // 1. Push live parameter changes to the engine.
    engine_.setGravityMagnitude(params.getFloat("gravityMagnitude"));
    engine_.setFlipBlend       (params.getFloat("flipBlend"));
    engine_.setJacobiIterations(params.getInt  ("jacobiIterations"));

    // 2. Handle refill request (edge-triggered: fire once per user toggle to "refill").
    const bool refillActive = (params.getString("refill") == "refill");
    if (refillActive && !lastRefillWasActive_) {
        engine_.refill(params.getFloat("fillLevel"));
    }
    lastRefillWasActive_ = refillActive;

    // 3. Read orientation and push to the engine on change.
    const Gravity g = activeSource().getGravity();
    if (g.x != lastGravity_.x || g.y != lastGravity_.y || g.z != lastGravity_.z) {
        engine_.setGravity(g);
        lastGravity_ = g;
    }

    // 4. Step the physics (dt = one frame at 60 Hz).
    engine_.update(1.0f / 60.0f);

    // 5. Clear canvas and render.
    clear();
    engine_.renderSurface([&](int x, int y, int z, uint32_t c) {
        setPixel3D(x, y, z, unpackColor(c));
    });

    render();
    ++frame_;
    return true;
}
