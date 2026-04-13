#include "SandCube.h"

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

SandCube::SandCube(std::string serverUri, bool imuDebug)
    : CubeApplication(60, serverUri, "SandCube")
    , engine_()
    , spawner_(SandSpawner::DEFAULT_EVERY_N, SandSpawner::DEFAULT_BURST)
    , imu_()
    , imuDebug_(imuDebug)
{
    std::cout << "SandCube initialised. Connecting to: " << serverUri << std::endl;

    // Live-tunable parameters exposed to the CubeWebapp.
    params.registerInt  ("spawnEveryN", "Spawn period (frames)", 1, 30,
                         SandSpawner::DEFAULT_EVERY_N, "Sand");
    params.registerInt  ("burst",       "Grains per spawn",      0, 200,
                         SandSpawner::DEFAULT_BURST,  "Sand");
    params.registerFloat("fade",        "Trail fade",            0.0f, 1.0f,
                         0.0f, 0.05f,                "Sand");
    params.registerEnum ("orientation", "Gravity source",
                         {"IMU", "Keyboard"}, "IMU",  "Control");

    // Prime the engine with a sensible starting gravity.
    engine_.setGravity(lastGravity_);
}

// ──────────────────────────────────────────────────────────────────────────

OrientationSource& SandCube::activeSource() {
    const std::string mode = params.getString("orientation");
    if (mode == "Keyboard") {
        if (!keyboard_) {
            keyboard_ = std::make_unique<KeyboardTilt>();
        }
        return *keyboard_;
    }
    return imu_;
}

// ──────────────────────────────────────────────────────────────────────────

bool SandCube::loop() {
    // 1. Pick up live parameter changes.
    spawner_.setSpawnEveryN(params.getInt("spawnEveryN"));
    spawner_.setBurst     (params.getInt("burst"));
    const float fadeAmount = params.getFloat("fade");

    // 2. Read orientation and push to the engine on change.
    const Gravity g = activeSource().getGravity();
    if (g.x != lastGravity_.x || g.y != lastGravity_.y || g.z != lastGravity_.z) {
        engine_.setGravity(g);
        lastGravity_ = g;
    }

    // 3. Spawn grains and step the physics.
    spawner_.tick(engine_, frame_, lastGravity_);
    engine_.update();

    // 4. Frame buffer management: full clear, or partial fade for a trail.
    if (fadeAmount > 0.0f) {
        fade(1.0f - fadeAmount);
    } else {
        clear();
    }

    // 5. Render the six outer faces of the voxel grid.
    renderSurface();

    // 6. IMU debug: show a red dot on the face the gravity vector points at,
    //    mirroring how ImuTest uses getCubeAccIntersect().
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

// ──────────────────────────────────────────────────────────────────────────
//  renderSurface
//
//  Only the outer shell of the 64³ voxel grid is visible on the physical
//  cube, so we walk the six face planes (24 576 pixels) instead of the
//  full 262 144 voxel volume. matrixserver owns all face-to-panel mapping
//  and rotation — we just call setPixel3D.
// ──────────────────────────────────────────────────────────────────────────
void SandCube::renderSurface() {
    const VoxelGrid& grid = engine_.getGrid();
    constexpr int MAX = CUBEMAXINDEX;           // 63
    constexpr int VMAX = VIRTUALCUBEMAXINDEX;   // 65

    // Map grid coordinates (0–63) to virtual cube coordinates that
    // setPixel3D expects.  The face axis must hit the virtual boundary
    // (0 or VMAX) so setPixel3D routes it to the correct screen.
    // The two free axes map grid [0..63] → virtual [1..64] so they
    // land on physical panel pixels [0..63].
    auto place = [&](int gx, int gy, int gz,
                     int vx, int vy, int vz) {
        const uint32_t c = grid.getCurrent(gx, gy, gz);
        if (c != 0) {
            setPixel3D(vx, vy, vz, unpackColor(c));
        }
    };

    // Face y = 0 (screen 0 – front) and y = MAX (screen 2 – back)
    for (int z = 0; z <= MAX; ++z)
        for (int x = 0; x <= MAX; ++x) {
            place(x, 0,   z,  x + 1, 0,    z + 1);
            place(x, MAX, z,  x + 1, VMAX, z + 1);
        }

    // Face z = MAX (screen 5 – bottom) and z = 0 (screen 4 – top)
    for (int y = 0; y <= MAX; ++y)
        for (int x = 0; x <= MAX; ++x) {
            place(x, y, MAX,  x + 1, y + 1, VMAX);
            place(x, y, 0,    x + 1, y + 1, 0);
        }

    // Face x = 0 (screen 3 – left) and x = MAX (screen 1 – right)
    for (int y = 0; y <= MAX; ++y)
        for (int z = 0; z <= MAX; ++z) {
            place(0,   y, z,  0,    y + 1, z + 1);
            place(MAX, y, z,  VMAX, y + 1, z + 1);
        }
}
