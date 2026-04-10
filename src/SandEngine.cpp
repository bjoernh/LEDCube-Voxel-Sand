#include "SandEngine.h"
#include "Hash.h"

#include <algorithm>   // std::sort

// ──────────────────────────────────────────────────────────────────────────

SandEngine::SandEngine(bool strictOneStep)
    : strictOneStep_(strictOneStep)
    , grid_()
    , gravity_{0, -1, 0}
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

    for (int dx = -1; dx <= 1; ++dx)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dz = -1; dz <= 1; ++dz) {
        if (dx == 0 && dy == 0 && dz == 0) continue;
        // Skip pure-gravity direction
        if (dx == gravity_.dx && dy == gravity_.dy && dz == gravity_.dz) continue;
        // Must have downward component
        const int dot = dx*gravity_.dx + dy*gravity_.dy + dz*gravity_.dz;
        if (dot > 0) {
            slideDirs_.push_back({dx, dy, dz});
        }
    }

    // Prefer more-downward diagonals (higher dot product) so piles
    // form under the particle rather than far to the side.
    std::sort(slideDirs_.begin(), slideDirs_.end(),
        [this](const SlideDir& a, const SlideDir& b) {
            const int dA = a.dx*gravity_.dx + a.dy*gravity_.dy + a.dz*gravity_.dz;
            const int dB = b.dx*gravity_.dx + b.dy*gravity_.dy + b.dz*gravity_.dz;
            return dA > dB;
        });
}

// ──────────────────────────────────────────────────────────────────────────

bool SandEngine::tryPlace(int nx, int ny, int nz, uint32_t color) noexcept {
    if (!grid_.inBounds(nx, ny, nz))       return false;
    if (grid_.getNext(nx, ny, nz) != 0)    return false;   // already claimed this tick

    if (strictOneStep_) {
        // Also reject cells that are occupied in the current buffer
        // to prevent cascade moves within one tick.
        if (grid_.getCurrent(nx, ny, nz) != 0) return false;
    }

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
        const int tx = x + gravity_.dx;
        const int ty = y + gravity_.dy;
        const int tz = z + gravity_.dz;

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
