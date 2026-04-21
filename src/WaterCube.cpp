#include "WaterCube.h"

#include <algorithm>
#include <cmath>
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

WaterCube::WaterCube(std::string serverUri, bool imuDebug, bool profile)
    : CubeApplication(60, serverUri, "WaterCube")
    , engine_()
    , imu_()
    , imuDebug_(imuDebug)
{
    std::cout << "WaterCube initialised. Connecting to: " << serverUri << std::endl;
    if (profile) {
        engine_.setProfile(true);
        std::cout << "Profiling enabled — per-phase timings printed every 60 frames.\n";
    }

    params.registerFloat("gravityMagnitude",    "Gravity (cells/s²)",   0.0f, 60.0f,
                         25.0f, 1.0f, "Physics");
    params.registerFloat("flipBlend",           "FLIP blend (0=PIC)",   0.0f,  1.0f,
                         0.95f, 0.05f, "Physics");
    params.registerInt  ("pressureIterations", "Pressure iters (SOR)", 2, 120,
                         40, "Physics");
    params.registerFloat("sorOmega",           "SOR ω",                1.0f,  1.95f,
                         1.7f, 0.05f, "Physics");
    params.registerFloat("fillLevel",           "Fill level",           0.0f,  1.0f,
                         0.40f, 0.05f, "Fluid");
    params.registerEnum ("refill",              "Refill",
                         {"idle", "refill"}, "idle", "Fluid");
    params.registerEnum ("orientation",         "Gravity source",
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
    engine_.setGravityMagnitude  (params.getFloat("gravityMagnitude"));
    engine_.setFlipBlend         (params.getFloat("flipBlend"));
    engine_.setPressureIterations(params.getInt  ("pressureIterations"));
    engine_.setSORRelaxation     (params.getFloat("sorOmega"));

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
    //    FluidEngine delivers grid coordinates (0–63).  setPixel3D expects
    //    virtual cube coordinates where face boundaries are 0 and
    //    VIRTUALCUBEMAXINDEX (65) — grid MAX (63) must map to 65, and the two
    //    free axes shift by +1 so they land on physical panel pixels [0..63].
    clear();
    engine_.renderSurface([&](int x, int y, int z, uint32_t c) {
        auto toV = [](int v) -> int {
            if (v == 0)           return 0;
            if (v == CUBEMAXINDEX) return VIRTUALCUBEMAXINDEX;
            return v + 1;
        };
        setPixel3D(toV(x), toV(y), toV(z), unpackColor(c));
    });

    // 6. IMU debug: red dot on the face the gravity vector points at.
    if (imuDebug_) {
        const float ax = lastGravity_.x, ay = lastGravity_.y, az = lastGravity_.z;
        const float maxAbs = std::max({std::fabs(ax), std::fabs(ay), std::fabs(az)});
        if (maxAbs > 1e-6f) {
            const float s = 1.0f / maxAbs;
            const int cx = static_cast<int>(ax * s * 33.0f) + 33;
            const int cy = static_cast<int>(ay * s * 33.0f) + 33;
            const int cz = static_cast<int>(az * s * 33.0f) + 33;
            setPixel3D(cx, cy, cz, Color(255, 0, 0));
        }
    }

    render();
    ++frame_;
    return true;
}
