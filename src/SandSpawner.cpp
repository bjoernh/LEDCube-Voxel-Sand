#include "SandSpawner.h"
#include "Hash.h"

SandSpawner::SandSpawner(int spawnEveryN, int burst)
    : spawnEveryN_(spawnEveryN)
    , burst_(burst)
{}

void SandSpawner::tick(SandEngine& engine, int frame) {
    if (frame % spawnEveryN_ != 0) return;

    // Spawn along the top edge of the front face (z=63, y=63).
    // x is spread across the full width using a hash so grains are
    // distributed rather than clustered.
    const VoxelGrid& grid = engine.getGrid();

    for (int i = 0; i < burst_; ++i) {
        // Wang hash of (frame, i) mapped to [0, GRID_SIZE-1].
        const uint32_t h = wangHash(
            static_cast<uint32_t>(frame) * 2'246'822'519u +
            static_cast<uint32_t>(i)     * 2'654'435'761u);

        const int sx = static_cast<int>(h % static_cast<uint32_t>(GRID_SIZE));
        const int sy = GRID_SIZE - 1;   // top row
        const int sz = GRID_SIZE - 1;   // front face (z=63)

        const uint32_t color =
            DEFAULT_PALETTE[static_cast<std::size_t>(frame + i) % DEFAULT_PALETTE.size()];

        if (grid.getCurrent(sx, sy, sz) == 0) {
            engine.spawnSand(sx, sy, sz, color);
        }
    }
}
