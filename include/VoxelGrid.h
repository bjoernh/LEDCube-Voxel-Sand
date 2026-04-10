#pragma once

#include <vector>
#include <cstdint>
#include <stdexcept>

// ──────────────────────────────────────────────────────────────────────────
//  VoxelGrid
//
//  64 × 64 × 64 flat voxel store with double buffering.
//
//  Memory layout  :  index = (z * GRID_SIZE + y) * GRID_SIZE + x
//  Voxel encoding :  0x00000000 → empty
//                    0x00RRGGBB → occupied (sand color)
//
//  Double-buffer contract
//    • Physics reads  getCurrent()  → never changes during an update tick.
//    • Physics writes setNext()     → only visible after swap().
//    • swap()  makes  next  the new  current  in O(1) (pointer swap).
// ──────────────────────────────────────────────────────────────────────────

static constexpr int GRID_SIZE  = 64;
static constexpr int GRID_TOTAL = GRID_SIZE * GRID_SIZE * GRID_SIZE; // 262 144

class VoxelGrid {
public:
    VoxelGrid();

    // ── Index helpers ───────────────────────────────────────────────────────

    [[nodiscard]] inline int getIndex(int x, int y, int z) const noexcept {
        return (z * GRID_SIZE + y) * GRID_SIZE + x;
    }

    /// Branchless bounds check using unsigned underflow.
    [[nodiscard]] inline bool inBounds(int x, int y, int z) const noexcept {
        return static_cast<unsigned>(x) < static_cast<unsigned>(GRID_SIZE) &&
               static_cast<unsigned>(y) < static_cast<unsigned>(GRID_SIZE) &&
               static_cast<unsigned>(z) < static_cast<unsigned>(GRID_SIZE);
    }

    // ── Read from the CURRENT (stable) buffer ──────────────────────────────

    [[nodiscard]] inline uint32_t getCurrent(int x, int y, int z) const noexcept {
        return current_[getIndex(x, y, z)];
    }

    [[nodiscard]] inline uint32_t getCurrentByIdx(int idx) const noexcept {
        return current_[idx];
    }

    // ── Read / write the NEXT (work-in-progress) buffer ────────────────────

    [[nodiscard]] inline uint32_t getNext(int x, int y, int z) const noexcept {
        return next_[getIndex(x, y, z)];
    }

    inline void setNext(int x, int y, int z, uint32_t value) noexcept {
        next_[getIndex(x, y, z)] = value;
    }

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /// Zero-fill the next buffer.  Call before every physics tick.
    void clearNext() noexcept;

    /// Swap current ↔ next in O(1).  Call after every physics tick.
    void swap() noexcept;

    // ── Spawning ───────────────────────────────────────────────────────────

    /// Write directly to the current buffer (used before a tick begins).
    /// Throws std::out_of_range if coordinates are invalid.
    void spawnSand(int x, int y, int z, uint32_t color);

    // ── Direct buffer access (for the face mapper) ─────────────────────────
    [[nodiscard]] const std::vector<uint32_t>& getCurrentGrid() const noexcept {
        return current_;
    }

private:
    std::vector<uint32_t> current_;
    std::vector<uint32_t> next_;
};
