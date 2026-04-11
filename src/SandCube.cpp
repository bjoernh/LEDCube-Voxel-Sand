#include "SandCube.h"

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

SandCube::SandCube(std::string serverUri)
    : CubeApplication(60, serverUri, "SandCube")
    , engine_()
    , spawner_(SandSpawner::DEFAULT_EVERY_N, SandSpawner::DEFAULT_BURST)
    , imu_()
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
    constexpr int MAX = CUBEMAXINDEX;   // 63

    auto place = [&](int x, int y, int z) {
        const uint32_t c = grid.getCurrent(x, y, z);
        if (c != 0) {
            setPixel3D(x, y, z, unpackColor(c));
        }
    };

    // Top (y = MAX) and bottom (y = 0) faces
    for (int z = 0; z <= MAX; ++z)
        for (int x = 0; x <= MAX; ++x) {
            place(x, MAX, z);
            place(x, 0,   z);
        }

    // Front (z = MAX) and back (z = 0) faces
    for (int y = 0; y <= MAX; ++y)
        for (int x = 0; x <= MAX; ++x) {
            place(x, y, MAX);
            place(x, y, 0);
        }

    // Left (x = 0) and right (x = MAX) faces
    for (int y = 0; y <= MAX; ++y)
        for (int z = 0; z <= MAX; ++z) {
            place(0,   y, z);
            place(MAX, y, z);
        }
}
