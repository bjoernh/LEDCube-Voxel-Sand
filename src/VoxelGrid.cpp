#include "VoxelGrid.h"

#include <algorithm>   // std::fill

// ──────────────────────────────────────────────────────────────────────────

VoxelGrid::VoxelGrid()
    : current_(GRID_TOTAL, 0u)
    , next_   (GRID_TOTAL, 0u)
{}

void VoxelGrid::clearNext() noexcept {
    std::fill(next_.begin(), next_.end(), 0u);
}

void VoxelGrid::swap() noexcept {
    // O(1) pointer swap — no data copies.
    current_.swap(next_);
}

void VoxelGrid::spawnSand(int x, int y, int z, uint32_t color) {
    if (!inBounds(x, y, z)) {
        throw std::out_of_range(
            "VoxelGrid::spawnSand — coordinates out of bounds");
    }
    current_[getIndex(x, y, z)] = color;
}
