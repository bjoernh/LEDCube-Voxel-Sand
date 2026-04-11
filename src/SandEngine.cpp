#include "SandEngine.h"
#include "Hash.h"

#include <algorithm>   // std::sort

// ──────────────────────────────────────────────────────────────────────────

SandEngine::SandEngine()
    : grid_()
    , gravity_{0.0f, -1.0f, 0.0f}
    , primary_dx_{0}
    , primary_dy_{-1}
    , primary_dz_{0}
{
    rebuildSlideDirs();
}

void SandEngine::setGravity(Gravity g) {
    gravity_ = g;
    rebuildSlideDirs();
}

// ──────────────────────────────────────────────────────────────────────────
//  rebuildSlideDirs
//
//  For a given gravity g, collect all 26-neighbour directions v such that:
//    • v ≠ (0,0,0)
//    • v ≠ g  (that is the primary-fall direction, handled separately)
//    • dot(v, g) > 0  (v has a component in the direction of gravity)
//
//  Sort by dot(v,g) descending so the most-downward diagonals are tried
//  first before the more-sideways ones.
//
//  For gravity (0,-1,0) this yields exactly the 8 diagonals:
//    (±1,−1,0), (0,−1,±1), (±1,−1,±1)
// ──────────────────────────────────────────────────────────────────────────
void SandEngine::rebuildSlideDirs() {
    slideDirs_.clear();

    // Determine the primary discrete direction based on highest dot product
    int best_dx = 0, best_dy = 0, best_dz = 0;
    float max_dot = -1e9f;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (dx == 0 && dy == 0 && dz == 0) continue;
                float dot = dx * gravity_.x + dy * gravity_.y + dz * gravity_.z;
                if (dot > max_dot) {
                    max_dot = dot;
                    best_dx = dx;
                    best_dy = dy;
                    best_dz = dz;
                }
            }
        }
    }
    primary_dx_ = best_dx;
    primary_dy_ = best_dy;
    primary_dz_ = best_dz;

    for (int dx = -1; dx <= 1; ++dx)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dz = -1; dz <= 1; ++dz) {
        if (dx == 0 && dy == 0 && dz == 0) continue;
        // Skip primary direction
        if (dx == primary_dx_ && dy == primary_dy_ && dz == primary_dz_) continue;
        // Must have downward component
        const float dot = dx * gravity_.x + dy * gravity_.y + dz * gravity_.z;
        if (dot > 0.0f) {
            slideDirs_.push_back({dx, dy, dz});
        }
    }

    // Prefer more-downward diagonals (higher dot product)
    std::sort(slideDirs_.begin(), slideDirs_.end(),
        [this](const SlideDir& a, const SlideDir& b) {
            const float dA = a.dx * gravity_.x + a.dy * gravity_.y + a.dz * gravity_.z;
            const float dB = b.dx * gravity_.x + b.dy * gravity_.y + b.dz * gravity_.z;
            return dA > dB;
        });
}

// ──────────────────────────────────────────────────────────────────────────

bool SandEngine::tryPlace(int nx, int ny, int nz, uint32_t color) noexcept {
    if (!grid_.inBounds(nx, ny, nz))       return false;
    if (grid_.getNext(nx, ny, nz) != 0)    return false;   // already claimed this tick
    if (grid_.getCurrent(nx, ny, nz) != 0) return false;

    grid_.setNext(nx, ny, nz, color);
    return true;
}

// ──────────────────────────────────────────────────────────────────────────
//  update
//
//  One full simulation tick:
//    1. Zero-fill next_.
//    2. For each occupied voxel in current_:
//         Rule A – try primary gravity step.
//         Rule B – try each slide direction in a per-particle random order.
//         Rule C – write the particle back to its own position in next_.
//    3. swap() current_ ↔ next_.
//
//  Anti-bias strategy
//  ──────────────────
//  Shuffling the slide-direction array for every particle would require a
//  copy (up to 8 elements) per iteration — 262 144 copies/tick, ~2 MB.
//  Instead we derive a cheap per-particle starting index from a Wang hash
//  of the voxel's grid address XOR'd with the frame counter, then rotate
//  through the pre-sorted slideDirs_ array.  This produces no systematic
//  spatial bias while staying fully allocation-free in the hot loop.
// ──────────────────────────────────────────────────────────────────────────
void SandEngine::update() {
    grid_.clearNext();

    const int  n    = static_cast<int>(slideDirs_.size());
    const auto fc   = frameCount_;   // capture for hash

    for (int z = 0; z < GRID_SIZE; ++z)
    for (int y = 0; y < GRID_SIZE; ++y)
    for (int x = 0; x < GRID_SIZE; ++x) {

        const uint32_t color = grid_.getCurrent(x, y, z);
        if (color == 0) continue;

        // ── Rule A : primary gravity step ─────────────────────────────────
        const int tx = x + primary_dx_;
        const int ty = y + primary_dy_;
        const int tz = z + primary_dz_;

        if (tryPlace(tx, ty, tz, color)) continue;

        // ── Rule B : diagonal slide (randomised start index) ──────────────
        if (n > 0) {
            // Encode (x,y,z) into a single integer for hashing.
            // Offset by a large prime × frame to decorrelate frames.
            const uint32_t addr  = static_cast<uint32_t>(
                (z * GRID_SIZE + y) * GRID_SIZE + x);
            const uint32_t seed  = addr ^ (fc * 2'246'822'519u);
            const int      start = static_cast<int>(wangHash(seed) % static_cast<uint32_t>(n));

            bool moved = false;
            for (int i = 0; i < n; ++i) {
                const SlideDir& s = slideDirs_[(start + i) % n];
                if (tryPlace(x + s.dx, y + s.dy, z + s.dz, color)) {
                    moved = true;
                    break;
                }
            }
            if (moved) continue;
        }

        // ── Rule C : stay in place ─────────────────────────────────────────
        grid_.setNext(x, y, z, color);
    }

    grid_.swap();
    ++frameCount_;
}

// ──────────────────────────────────────────────────────────────────────────

void SandEngine::spawnSand(int x, int y, int z, uint32_t color) {
    grid_.spawnSand(x, y, z, color);
}
