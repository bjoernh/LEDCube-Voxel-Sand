#pragma once

#include "VoxelGrid.h"
#include <vector>
#include <array>
#include <cstdint>

// ──────────────────────────────────────────────────────────────────────────
//  Gravity
//
//  Discrete 3-D gravity vector.  Each component must be in {-1, 0, 1}.
//  Examples:
//    {0, -1,  0}  →  falling downward  (default)
//    {0,  1,  0}  →  floating upward
//    {1,  0,  0}  →  falling toward +x
// ──────────────────────────────────────────────────────────────────────────
struct Gravity {
    int dx = 0;
    int dy = -1;
    int dz = 0;
};

// ──────────────────────────────────────────────────────────────────────────
//  SandEngine
//
//  Owns the VoxelGrid and runs the 3-D cellular automata every tick.
//
//  Physics rules (per occupied voxel, each tick)
//  ──────────────────────────────────────────────
//  Rule A  – Primary move    : try (x+gx, y+gy, z+gz)
//  Rule B  – Diagonal slide  : if A is blocked, try up to 8 diagonal
//                              neighbours that share the gravity component.
//                              The order is randomised per-particle via a
//                              fast position hash to eliminate directional
//                              bias without dynamic allocation.
//  Rule C  – Stay in place   : if all slide attempts fail.
//
//  Double-buffer semantics
//  ───────────────────────
//  Decisions are made by reading current_; placements write to next_.
//  tryPlace() checks next_ for conflicts (not current_): a cell vacated
//  earlier in the same tick is visible as empty, which produces the
//  cascade / flow effect common in visual sand simulators.
//  Set STRICT_ONE_STEP = true in SandEngine.cpp to disable cascades.
// ──────────────────────────────────────────────────────────────────────────
class SandEngine {
public:
    /// strictOneStep: if true, each particle advances at most one cell per tick
    ///   (no cascade flow).  Default false = cascade / fluid-like behaviour.
    explicit SandEngine(bool strictOneStep = false);

    // ── Configuration ──────────────────────────────────────────────────────

    /// Update gravity and recompute slide-direction table.
    void setGravity(Gravity g);

    [[nodiscard]] Gravity getGravity() const noexcept { return gravity_; }

    // ── Per-frame API ──────────────────────────────────────────────────────

    /// Advance simulation by one tick (clear → evaluate → swap).
    void update();

    /// Inject a sand grain into the current buffer before the next tick.
    void spawnSand(int x, int y, int z, uint32_t color);

    // ── Access the grid (e.g. for the face mapper) ─────────────────────────
    [[nodiscard]] const VoxelGrid& getGrid() const noexcept { return grid_; }
    [[nodiscard]]       VoxelGrid& getGrid()       noexcept { return grid_; }

private:
    // ── Slide direction entry ───────────────────────────────────────────────
    struct SlideDir { int dx, dy, dz; };

    /// Rebuild slideDirs_ whenever gravity changes.
    void rebuildSlideDirs();

    /// Attempt to place a grain at (nx,ny,nz) in next_.
    /// Returns true and writes color if the cell is in-bounds and unclaimed.
    [[nodiscard]] bool tryPlace(int nx, int ny, int nz, uint32_t color) noexcept;

    // ── State ───────────────────────────────────────────────────────────────
    const bool             strictOneStep_;
    VoxelGrid              grid_;
    Gravity                gravity_{};
    std::vector<SlideDir>  slideDirs_;   ///< diagonal candidates, sorted by dot(v,g) ↓
    uint32_t               frameCount_{0};
};
