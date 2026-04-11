#include "SandSpawner.h"
#include "Hash.h"

#include <cmath>

SandSpawner::SandSpawner(int spawnEveryN, int burst)
    : spawnEveryN_(spawnEveryN > 0 ? spawnEveryN : 1)
    , burst_(burst >= 0 ? burst : 0)
{}

// ──────────────────────────────────────────────────────────────────────────
//  Spawn-face selection
//
//  Pick the axis whose gravity component has the largest magnitude. Grains
//  enter from the face opposite that direction. The two remaining axes are
//  used as free parameters and filled with a Wang hash so grains are
//  spread across the whole face rather than stacking in one column.
//
//  Examples:
//    g = (0,-1, 0)  → main axis = Y (neg) → spawn plane y=63 (top)
//    g = (1, 0, 0)  → main axis = X (pos) → spawn plane x=0  (left)
//    g = (0, 0,-1)  → main axis = Z (neg) → spawn plane z=63 (front)
// ──────────────────────────────────────────────────────────────────────────
void SandSpawner::tick(SandEngine& engine, int frame, const Gravity& gravity) {
    if (burst_ <= 0) return;
    if (frame % spawnEveryN_ != 0) return;

    const float ax = std::fabs(gravity.x);
    const float ay = std::fabs(gravity.y);
    const float az = std::fabs(gravity.z);

    // Default: gravity pointing in -Y (standard "down").
    int   mainAxis = 1;                                       // 0=x, 1=y, 2=z
    float mainSign = (gravity.y >= 0.0f) ? 1.0f : -1.0f;

    if (ax >= ay && ax >= az) {
        mainAxis = 0;
        mainSign = (gravity.x >= 0.0f) ? 1.0f : -1.0f;
    } else if (az >= ay && az >= ax) {
        mainAxis = 2;
        mainSign = (gravity.z >= 0.0f) ? 1.0f : -1.0f;
    }

    // Fixed coordinate on the spawn face: grains enter from the side
    // opposite the gravity direction.
    const int fixedCoord = (mainSign < 0.0f) ? (GRID_SIZE - 1) : 0;

    const VoxelGrid& grid = engine.getGrid();

    for (int i = 0; i < burst_; ++i) {
        // Two independent hashes for the two free axes on the spawn face.
        const uint32_t h1 = wangHash(
            static_cast<uint32_t>(frame) * 2'246'822'519u +
            static_cast<uint32_t>(i)     * 2'654'435'761u);
        const uint32_t h2 = wangHash(h1 ^ 0x9E3779B9u);

        const int a = static_cast<int>(h1 % static_cast<uint32_t>(GRID_SIZE));
        const int b = static_cast<int>(h2 % static_cast<uint32_t>(GRID_SIZE));

        int sx = 0, sy = 0, sz = 0;
        switch (mainAxis) {
            case 0: sx = fixedCoord; sy = a;          sz = b;          break;
            case 1: sx = a;          sy = fixedCoord; sz = b;          break;
            case 2: sx = a;          sy = b;          sz = fixedCoord; break;
        }

        const uint32_t color =
            DEFAULT_PALETTE[static_cast<std::size_t>(frame + i) % DEFAULT_PALETTE.size()];

        if (grid.getCurrent(sx, sy, sz) == 0) {
            engine.spawnSand(sx, sy, sz, color);
        }
    }
}
