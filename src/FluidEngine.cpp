#include "FluidEngine.h"
#include "VoxelGrid.h"   // for GRID_SIZE constant

#include <Eigen/Core>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <utility>
#include <vector>

// ─── Render-grid size ───────────────────────────────────────────────────────
// FluidEngine::RN and GRID_SIZE must agree; static_assert catches a mismatch.
static_assert(FluidEngine::RN == GRID_SIZE, "FluidEngine::RN must match GRID_SIZE");
static constexpr int RN    = FluidEngine::RN;   // 64
static constexpr int RMAX  = RN - 1;            // 63

// Sim-interior cell range is [1, SIM_N-1], which spans (SIM_N-2) cells.
// Map that span onto [0, RN) in render-pixel space.
static constexpr float SIM_TO_RENDER =
    static_cast<float>(RN) / static_cast<float>(FluidEngine::SIM_N - 2);

// ─── Shading palette ─────────────────────────────────────────────────────────
static constexpr uint8_t SHALLOW_R = 80,  SHALLOW_G = 180, SHALLOW_B = 255;
static constexpr uint8_t DEEP_R    =  0,  DEEP_G    =  30, DEEP_B    = 110;
static constexpr uint8_t HIGH_AMT  = 40;  // additive highlight on surface cells

static inline uint32_t packColor(uint8_t r, uint8_t g, uint8_t b) noexcept {
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

static inline uint32_t shadeWater(float t, bool isSurface) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    auto lerp8 = [t](uint8_t a, uint8_t b) -> uint8_t {
        return static_cast<uint8_t>(a + t * (static_cast<float>(b) - static_cast<float>(a)));
    };
    uint8_t r = lerp8(SHALLOW_R, DEEP_R);
    uint8_t g = lerp8(SHALLOW_G, DEEP_G);
    uint8_t b = lerp8(SHALLOW_B, DEEP_B);
    if (isSurface) {
        r = static_cast<uint8_t>(std::min(255, int(r) + HIGH_AMT));
        g = static_cast<uint8_t>(std::min(255, int(g) + HIGH_AMT));
        b = static_cast<uint8_t>(std::min(255, int(b) + HIGH_AMT));
    }
    return packColor(r, g, b);
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Clamp to [lo, hi] inclusive.
static inline int clampi(int v, int lo, int hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor — allocate all buffers once, refill to 40 %
// ─────────────────────────────────────────────────────────────────────────────
FluidEngine::FluidEngine()
    : u_   (U_SIZE, 0.0f), v_   (V_SIZE, 0.0f), w_   (W_SIZE, 0.0f)
    , uOld_(U_SIZE, 0.0f), vOld_(V_SIZE, 0.0f), wOld_(W_SIZE, 0.0f)
    , uW_  (U_SIZE, 0.0f), vW_  (V_SIZE, 0.0f), wW_  (W_SIZE, 0.0f)
    , pressure_  (C_SIZE, 0.0f)
    , divergence_(C_SIZE, 0.0f)
    , cellType_  (C_SIZE, CellType::AIR)
    , faceBuf_   (6 * RN_SQ, 0.0f)
{
    particles_.reserve(MAX_PARTICLES);
    refill(0.40f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  setGravity
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::setGravity(const Gravity& g) noexcept {
    gravity_ = g;
}

// ─────────────────────────────────────────────────────────────────────────────
//  refill — place 2 particles/cell in the "bottom" fillLevel fraction
//
//  "Bottom" = cells whose centre has the highest dot product with the current
//  gravity direction (gravity points "down", so downward cells have the most
//  positive projection onto g).
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::refill(float fillLevel) {
    fillLevel = std::clamp(fillLevel, 0.0f, 1.0f);
    particles_.clear();

    // Score each interior cell by its "downward-ness" along current gravity.
    // Interior = cells not in the solid outer layer (i,j,k each in [1,N-2]).
    const Eigen::Vector3f gv(gravity_.x, gravity_.y, gravity_.z);

    struct CellScore { float score; int i, j, k; };
    std::vector<CellScore> scores;
    scores.reserve((SIM_N - 2) * (SIM_N - 2) * (SIM_N - 2));

    for (int k = 1; k <= SIM_N - 2; ++k)
    for (int j = 1; j <= SIM_N - 2; ++j)
    for (int i = 1; i <= SIM_N - 2; ++i) {
        Eigen::Vector3f centre(i + 0.5f, j + 0.5f, k + 0.5f);
        scores.push_back({ gv.dot(centre), i, j, k });
    }

    std::sort(scores.begin(), scores.end(),
        [](const CellScore& a, const CellScore& b) { return a.score > b.score; });

    const int numFluid = static_cast<int>(scores.size() * fillLevel);

    // Two deterministic jitter offsets per cell (avoid perfectly regular lattice)
    for (int n = 0; n < numFluid && particles_.size() < static_cast<std::size_t>(MAX_PARTICLES); ++n) {
        const auto& cs = scores[n];
        // Particle 1: lower-left quarter
        particles_.push_back({ Eigen::Vector3f(cs.i + 0.25f, cs.j + 0.25f, cs.k + 0.25f),
                                Eigen::Vector3f::Zero() });
        if (particles_.size() < static_cast<std::size_t>(MAX_PARTICLES)) {
            // Particle 2: upper-right quarter
            particles_.push_back({ Eigen::Vector3f(cs.i + 0.75f, cs.j + 0.75f, cs.k + 0.75f),
                                    Eigen::Vector3f::Zero() });
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  classifyCells — rebuild cellType_ from particle positions each tick.
//  Also accumulates the fluid centroid (cached for the renderer's depth shade).
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::classifyCells() {
    // Solid boundary: outermost layer
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        cellType_[cidx(i,j,k)] =
            (i == 0 || i == SIM_N-1 || j == 0 || j == SIM_N-1 || k == 0 || k == SIM_N-1)
            ? CellType::SOLID : CellType::AIR;
    }
    // Mark FLUID where particles reside
    for (const auto& p : particles_) {
        int ci = static_cast<int>(p.pos.x());
        int cj = static_cast<int>(p.pos.y());
        int ck = static_cast<int>(p.pos.z());
        ci = clampi(ci, 0, SIM_N-1);
        cj = clampi(cj, 0, SIM_N-1);
        ck = clampi(ck, 0, SIM_N-1);
        if (cellType_[cidx(ci,cj,ck)] != CellType::SOLID)
            cellType_[cidx(ci,cj,ck)] = CellType::FLUID;
    }

    // Fluid-cell centroid (in sim-cell units). Used by the renderer to shade
    // depth along the gravity axis. Folded into this O(N³) pass for free.
    Eigen::Vector3f sum = Eigen::Vector3f::Zero();
    int count = 0;
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        if (cellType_[cidx(i,j,k)] == CellType::FLUID) {
            sum += Eigen::Vector3f(i + 0.5f, j + 0.5f, k + 0.5f);
            ++count;
        }
    }
    fluidCount_ = count;
    centroid_   = (count > 0) ? (sum / static_cast<float>(count))
                              : Eigen::Vector3f(SIM_N * 0.5f, SIM_N * 0.5f, SIM_N * 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Trilinear interpolation on the three staggered velocity fields
//
//  u is sampled at (i,   j+0.5, k+0.5)  →  integer x, half-cell y,z
//  v is sampled at (i+0.5, j,   k+0.5)
//  w is sampled at (i+0.5, j+0.5, k  )
// ─────────────────────────────────────────────────────────────────────────────
float FluidEngine::trilerp_u(float px, float py, float pz) const noexcept {
    // Shift so that index i=0 is at x=0 (no offset in x for u)
    float fx = px,        fy = py - 0.5f, fz = pz - 0.5f;
    int i0 = static_cast<int>(std::floor(fx));
    int j0 = static_cast<int>(std::floor(fy));
    int k0 = static_cast<int>(std::floor(fz));
    float tx = fx - i0, ty = fy - j0, tz = fz - k0;

    // Clamp corners to valid u-face range
    int i1 = clampi(i0+1, 0, SIM_N);   i0 = clampi(i0, 0, SIM_N);
    int j1 = clampi(j0+1, 0, SIM_N-1); j0 = clampi(j0, 0, SIM_N-1);
    int k1 = clampi(k0+1, 0, SIM_N-1); k0 = clampi(k0, 0, SIM_N-1);

    return (1-tx)*(1-ty)*(1-tz)*u_[uidx(i0,j0,k0)]
          + tx   *(1-ty)*(1-tz)*u_[uidx(i1,j0,k0)]
          +(1-tx)*  ty  *(1-tz)*u_[uidx(i0,j1,k0)]
          + tx   *  ty  *(1-tz)*u_[uidx(i1,j1,k0)]
          +(1-tx)*(1-ty)*  tz  *u_[uidx(i0,j0,k1)]
          + tx   *(1-ty)*  tz  *u_[uidx(i1,j0,k1)]
          +(1-tx)*  ty  *  tz  *u_[uidx(i0,j1,k1)]
          + tx   *  ty  *  tz  *u_[uidx(i1,j1,k1)];
}

float FluidEngine::trilerp_v(float px, float py, float pz) const noexcept {
    float fx = px - 0.5f, fy = py,        fz = pz - 0.5f;
    int i0 = static_cast<int>(std::floor(fx));
    int j0 = static_cast<int>(std::floor(fy));
    int k0 = static_cast<int>(std::floor(fz));
    float tx = fx - i0, ty = fy - j0, tz = fz - k0;

    int i1 = clampi(i0+1, 0, SIM_N-1); i0 = clampi(i0, 0, SIM_N-1);
    int j1 = clampi(j0+1, 0, SIM_N);   j0 = clampi(j0, 0, SIM_N);
    int k1 = clampi(k0+1, 0, SIM_N-1); k0 = clampi(k0, 0, SIM_N-1);

    return (1-tx)*(1-ty)*(1-tz)*v_[vidx(i0,j0,k0)]
          + tx   *(1-ty)*(1-tz)*v_[vidx(i1,j0,k0)]
          +(1-tx)*  ty  *(1-tz)*v_[vidx(i0,j1,k0)]
          + tx   *  ty  *(1-tz)*v_[vidx(i1,j1,k0)]
          +(1-tx)*(1-ty)*  tz  *v_[vidx(i0,j0,k1)]
          + tx   *(1-ty)*  tz  *v_[vidx(i1,j0,k1)]
          +(1-tx)*  ty  *  tz  *v_[vidx(i0,j1,k1)]
          + tx   *  ty  *  tz  *v_[vidx(i1,j1,k1)];
}

float FluidEngine::trilerp_w(float px, float py, float pz) const noexcept {
    float fx = px - 0.5f, fy = py - 0.5f, fz = pz;
    int i0 = static_cast<int>(std::floor(fx));
    int j0 = static_cast<int>(std::floor(fy));
    int k0 = static_cast<int>(std::floor(fz));
    float tx = fx - i0, ty = fy - j0, tz = fz - k0;

    int i1 = clampi(i0+1, 0, SIM_N-1); i0 = clampi(i0, 0, SIM_N-1);
    int j1 = clampi(j0+1, 0, SIM_N-1); j0 = clampi(j0, 0, SIM_N-1);
    int k1 = clampi(k0+1, 0, SIM_N);   k0 = clampi(k0, 0, SIM_N);

    return (1-tx)*(1-ty)*(1-tz)*w_[widx(i0,j0,k0)]
          + tx   *(1-ty)*(1-tz)*w_[widx(i1,j0,k0)]
          +(1-tx)*  ty  *(1-tz)*w_[widx(i0,j1,k0)]
          + tx   *  ty  *(1-tz)*w_[widx(i1,j1,k0)]
          +(1-tx)*(1-ty)*  tz  *w_[widx(i0,j0,k1)]
          + tx   *(1-ty)*  tz  *w_[widx(i1,j0,k1)]
          +(1-tx)*  ty  *  tz  *w_[widx(i0,j1,k1)]
          + tx   *  ty  *  tz  *w_[widx(i1,j1,k1)];
}

// ─────────────────────────────────────────────────────────────────────────────
//  transferP2G — scatter particle velocities to the MAC grid faces
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::transferP2G() {
    std::fill(u_.begin(), u_.end(), 0.0f); std::fill(uW_.begin(), uW_.end(), 0.0f);
    std::fill(v_.begin(), v_.end(), 0.0f); std::fill(vW_.begin(), vW_.end(), 0.0f);
    std::fill(w_.begin(), w_.end(), 0.0f); std::fill(wW_.begin(), wW_.end(), 0.0f);

    for (const auto& p : particles_) {
        const float px = p.pos.x(), py = p.pos.y(), pz = p.pos.z();

        // ── u ──────────────────────────────────────────────────────────────
        {
            float fx = px, fy = py - 0.5f, fz = pz - 0.5f;
            int i0 = static_cast<int>(std::floor(fx));
            int j0 = static_cast<int>(std::floor(fy));
            int k0 = static_cast<int>(std::floor(fz));
            float tx = fx - i0, ty = fy - j0, tz = fz - k0;
            int i1 = i0+1, j1 = j0+1, k1 = k0+1;

            // Clamp to valid u-face range
            auto addU = [&](int ii, int jj, int kk, float w) {
                if (ii < 0 || ii > SIM_N || jj < 0 || jj >= SIM_N || kk < 0 || kk >= SIM_N) return;
                int idx = uidx(ii, jj, kk);
                u_[idx]  += w * p.vel.x();
                uW_[idx] += w;
            };
            addU(i0,j0,k0, (1-tx)*(1-ty)*(1-tz));
            addU(i1,j0,k0,    tx *(1-ty)*(1-tz));
            addU(i0,j1,k0, (1-tx)*   ty *(1-tz));
            addU(i1,j1,k0,    tx *   ty *(1-tz));
            addU(i0,j0,k1, (1-tx)*(1-ty)*   tz );
            addU(i1,j0,k1,    tx *(1-ty)*   tz );
            addU(i0,j1,k1, (1-tx)*   ty *   tz );
            addU(i1,j1,k1,    tx *   ty *   tz );
        }

        // ── v ──────────────────────────────────────────────────────────────
        {
            float fx = px - 0.5f, fy = py, fz = pz - 0.5f;
            int i0 = static_cast<int>(std::floor(fx));
            int j0 = static_cast<int>(std::floor(fy));
            int k0 = static_cast<int>(std::floor(fz));
            float tx = fx - i0, ty = fy - j0, tz = fz - k0;
            int i1 = i0+1, j1 = j0+1, k1 = k0+1;

            auto addV = [&](int ii, int jj, int kk, float w) {
                if (ii < 0 || ii >= SIM_N || jj < 0 || jj > SIM_N || kk < 0 || kk >= SIM_N) return;
                int idx = vidx(ii, jj, kk);
                v_[idx]  += w * p.vel.y();
                vW_[idx] += w;
            };
            addV(i0,j0,k0, (1-tx)*(1-ty)*(1-tz));
            addV(i1,j0,k0,    tx *(1-ty)*(1-tz));
            addV(i0,j1,k0, (1-tx)*   ty *(1-tz));
            addV(i1,j1,k0,    tx *   ty *(1-tz));
            addV(i0,j0,k1, (1-tx)*(1-ty)*   tz );
            addV(i1,j0,k1,    tx *(1-ty)*   tz );
            addV(i0,j1,k1, (1-tx)*   ty *   tz );
            addV(i1,j1,k1,    tx *   ty *   tz );
        }

        // ── w ──────────────────────────────────────────────────────────────
        {
            float fx = px - 0.5f, fy = py - 0.5f, fz = pz;
            int i0 = static_cast<int>(std::floor(fx));
            int j0 = static_cast<int>(std::floor(fy));
            int k0 = static_cast<int>(std::floor(fz));
            float tx = fx - i0, ty = fy - j0, tz = fz - k0;
            int i1 = i0+1, j1 = j0+1, k1 = k0+1;

            auto addW = [&](int ii, int jj, int kk, float wt) {
                if (ii < 0 || ii >= SIM_N || jj < 0 || jj >= SIM_N || kk < 0 || kk > SIM_N) return;
                int idx = widx(ii, jj, kk);
                w_[idx]  += wt * p.vel.z();
                wW_[idx] += wt;
            };
            addW(i0,j0,k0, (1-tx)*(1-ty)*(1-tz));
            addW(i1,j0,k0,    tx *(1-ty)*(1-tz));
            addW(i0,j1,k0, (1-tx)*   ty *(1-tz));
            addW(i1,j1,k0,    tx *   ty *(1-tz));
            addW(i0,j0,k1, (1-tx)*(1-ty)*   tz );
            addW(i1,j0,k1,    tx *(1-ty)*   tz );
            addW(i0,j1,k1, (1-tx)*   ty *   tz );
            addW(i1,j1,k1,    tx *   ty *   tz );
        }
    }

    // Normalise by accumulated weights
    for (int i = 0; i < U_SIZE; ++i) if (uW_[i] > 1e-6f) u_[i] /= uW_[i];
    for (int i = 0; i < V_SIZE; ++i) if (vW_[i] > 1e-6f) v_[i] /= vW_[i];
    for (int i = 0; i < W_SIZE; ++i) if (wW_[i] > 1e-6f) w_[i] /= wW_[i];
}

// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::saveOldVelocities() {
    uOld_ = u_;
    vOld_ = v_;
    wOld_ = w_;
}

// ─────────────────────────────────────────────────────────────────────────────
//  addGravity — body force on interior non-solid faces adjacent to fluid
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::addGravity(float dt) {
    const float gx = gravity_.x * gravMag_ * dt;
    const float gy = gravity_.y * gravMag_ * dt;
    const float gz = gravity_.z * gravMag_ * dt;

    // u-faces: i in [1,N-1] (skip solid-wall faces at i=0 and i=N)
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 1; i < SIM_N; ++i) {
        if (cellType_[cidx(i-1,j,k)] == CellType::FLUID ||
            cellType_[cidx(i,  j,k)] == CellType::FLUID)
            u_[uidx(i,j,k)] += gx;
    }

    // v-faces: j in [1,N-1]
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 1; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        if (cellType_[cidx(i,j-1,k)] == CellType::FLUID ||
            cellType_[cidx(i,j,  k)] == CellType::FLUID)
            v_[vidx(i,j,k)] += gy;
    }

    // w-faces: k in [1,N-1]
    for (int k = 1; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        if (cellType_[cidx(i,j,k-1)] == CellType::FLUID ||
            cellType_[cidx(i,j,k  )] == CellType::FLUID)
            w_[widx(i,j,k)] += gz;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  enforceBoundary — zero normal velocity at all solid wall faces
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::enforceBoundary() {
    // 1. Zero normal velocities at boundary (forces fluid not to penetrate wall)
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j) {
        u_[uidx(1,       j, k)] = 0.0f;
        u_[uidx(SIM_N-1, j, k)] = 0.0f;
    }
    for (int k = 0; k < SIM_N; ++k)
    for (int i = 0; i < SIM_N; ++i) {
        v_[vidx(i, 1,       k)] = 0.0f;
        v_[vidx(i, SIM_N-1, k)] = 0.0f;
    }
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        w_[widx(i, j, 1      )] = 0.0f;
        w_[widx(i, j, SIM_N-1)] = 0.0f;
    }

    // 2. Free-slip: Copy tangential velocities into the solid boundary layer.
    // This prevents particles near the walls from artificially damping out their velocity
    // (which looks like sticky friction) due to trilinear interpolation with 0.

    // x-walls (i=0 and i=SIM_N-1)
    for (int k = 0; k < SIM_N; ++k) {
        for (int j = 0; j <= SIM_N; ++j) {
            v_[vidx(0,       j, k)] = v_[vidx(1,       j, k)];
            v_[vidx(SIM_N-1, j, k)] = v_[vidx(SIM_N-2, j, k)];
        }
    }
    for (int k = 0; k <= SIM_N; ++k) {
        for (int j = 0; j < SIM_N; ++j) {
            w_[widx(0,       j, k)] = w_[widx(1,       j, k)];
            w_[widx(SIM_N-1, j, k)] = w_[widx(SIM_N-2, j, k)];
        }
    }

    // y-walls (j=0 and j=SIM_N-1)
    for (int k = 0; k < SIM_N; ++k) {
        for (int i = 0; i <= SIM_N; ++i) {
            u_[uidx(i, 0,       k)] = u_[uidx(i, 1,       k)];
            u_[uidx(i, SIM_N-1, k)] = u_[uidx(i, SIM_N-2, k)];
        }
    }
    for (int k = 0; k <= SIM_N; ++k) {
        for (int i = 0; i < SIM_N; ++i) {
            w_[widx(i, 0,       k)] = w_[widx(i, 1,       k)];
            w_[widx(i, SIM_N-1, k)] = w_[widx(i, SIM_N-2, k)];
        }
    }

    // z-walls (k=0 and k=SIM_N-1)
    for (int j = 0; j < SIM_N; ++j) {
        for (int i = 0; i <= SIM_N; ++i) {
            u_[uidx(i, j, 0      )] = u_[uidx(i, j, 1      )];
            u_[uidx(i, j, SIM_N-1)] = u_[uidx(i, j, SIM_N-2)];
        }
    }
    for (int j = 0; j <= SIM_N; ++j) {
        for (int i = 0; i < SIM_N; ++i) {
            v_[vidx(i, j, 0      )] = v_[vidx(i, j, 1      )];
            v_[vidx(i, j, SIM_N-1)] = v_[vidx(i, j, SIM_N-2)];
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  computeDivergence — finite-difference divergence for FLUID cells
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::computeDivergence() {
    std::fill(divergence_.begin(), divergence_.end(), 0.0f);

    // Track particle count per cell to detect compression (volume loss)
    std::vector<int> pCount(C_SIZE, 0);
    for (const auto& p : particles_) {
        int ci = clampi(static_cast<int>(p.pos.x()), 0, SIM_N-1);
        int cj = clampi(static_cast<int>(p.pos.y()), 0, SIM_N-1);
        int ck = clampi(static_cast<int>(p.pos.z()), 0, SIM_N-1);
        pCount[cidx(ci, cj, ck)]++;
    }

    // Normal rest density is 2 particles per cell. Stiffness controls how hard
    // we push back when particles clump (which otherwise looks like volume loss).
    const float restDensity = 2.0f;
    const float stiffness = 2.0f;

    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        if (cellType_[cidx(i,j,k)] != CellType::FLUID) continue;

        float div = (u_[uidx(i+1,j,k)] - u_[uidx(i,j,k)])
                  + (v_[vidx(i,j+1,k)] - v_[vidx(i,j,k)])
                  + (w_[widx(i,j,k+1)] - w_[widx(i,j,k)]);

        // Artificial expansion if cell is over-compressed
        float err = static_cast<float>(pCount[cidx(i,j,k)]) - restDensity;
        if (err > 0.0f) {
            div -= stiffness * err;
        }

        divergence_[cidx(i,j,k)] = div;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  pressureSolve — Red-Black Gauss-Seidel with successive over-relaxation (SOR)
//
//  Replaces the previous Jacobi solver.  Red-Black colouring splits the grid
//  into two independent sets (i+j+k even/odd); within one colour there are no
//  same-colour neighbours, so we can update in place with the Gauss-Seidel
//  formula.  SOR relaxation (ω ≈ 1.7) accelerates convergence by ~4–5× over
//  Jacobi, so we run far fewer sweeps for the same residual.
//
//  Boundary conditions:
//    AIR neighbours → Dirichlet p=0 (pressure_ is always 0 for AIR cells)
//    SOLID neighbours → Neumann: excluded from the stencil (not counted)
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::pressureSolve() {
    // Warm-start: retain pressure from the previous frame for cells that are
    // still FLUID.  Only zero AIR and SOLID cells — they act as Dirichlet/Neumann
    // boundary conditions and must be 0 for neighbouring FLUID cells' stencils.
    // Keeping the previous FLUID pressure reduces the residual dramatically and
    // lets the solver converge with far fewer iterations, preventing volume loss.
    for (int c = 0; c < C_SIZE; ++c) {
        if (cellType_[c] != CellType::FLUID) pressure_[c] = 0.0f;
    }

    const float omega = sorOmega_;
    const int N = SIM_N;

    auto sweepColour = [&](int parity) noexcept {
        // Skip the outermost SOLID layer (i,j,k ∈ [1, N-2]).
        for (int k = 1; k < N - 1; ++k)
        for (int j = 1; j < N - 1; ++j) {
            // Start i so that (i+j+k)&1 == parity; increment by 2.
            int iStart = 1 + (((parity ^ j ^ k) & 1) ? 0 : 1);
            for (int i = iStart; i < N - 1; i += 2) {
                const int c = cidx(i, j, k);
                if (cellType_[c] != CellType::FLUID) continue;

                // 6-neighbour stencil, unrolled.
                float sum = 0.0f;
                int   count = 0;

                const CellType cXm = cellType_[cidx(i-1, j,   k  )];
                const CellType cXp = cellType_[cidx(i+1, j,   k  )];
                const CellType cYm = cellType_[cidx(i,   j-1, k  )];
                const CellType cYp = cellType_[cidx(i,   j+1, k  )];
                const CellType cZm = cellType_[cidx(i,   j,   k-1)];
                const CellType cZp = cellType_[cidx(i,   j,   k+1)];

                if (cXm != CellType::SOLID) { sum += pressure_[cidx(i-1, j,   k  )]; ++count; }
                if (cXp != CellType::SOLID) { sum += pressure_[cidx(i+1, j,   k  )]; ++count; }
                if (cYm != CellType::SOLID) { sum += pressure_[cidx(i,   j-1, k  )]; ++count; }
                if (cYp != CellType::SOLID) { sum += pressure_[cidx(i,   j+1, k  )]; ++count; }
                if (cZm != CellType::SOLID) { sum += pressure_[cidx(i,   j,   k-1)]; ++count; }
                if (cZp != CellType::SOLID) { sum += pressure_[cidx(i,   j,   k+1)]; ++count; }

                if (count > 0) {
                    const float target = (sum - divergence_[c]) / static_cast<float>(count);
                    pressure_[c] = (1.0f - omega) * pressure_[c] + omega * target;
                }
            }
        }
    };

    for (int iter = 0; iter < pressureIter_; ++iter) {
        sweepColour(0);  // red
        sweepColour(1);  // black
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  velocityProject — subtract pressure gradient from face velocities
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::velocityProject() {
    // u-faces: face at i is between cells (i-1,j,k) and (i,j,k)
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 1; i < SIM_N; ++i) {
        CellType cL = cellType_[cidx(i-1, j, k)];
        CellType cR = cellType_[cidx(i,   j, k)];
        if (cL == CellType::SOLID || cR == CellType::SOLID) {
            u_[uidx(i,j,k)] = 0.0f;
            continue;
        }
        if (cL == CellType::AIR && cR == CellType::AIR) continue;
        float pL = (cL == CellType::FLUID) ? pressure_[cidx(i-1,j,k)] : 0.0f;
        float pR = (cR == CellType::FLUID) ? pressure_[cidx(i,  j,k)] : 0.0f;
        u_[uidx(i,j,k)] -= (pR - pL);
    }

    // v-faces: face at j is between cells (i,j-1,k) and (i,j,k)
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 1; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        CellType cD = cellType_[cidx(i, j-1, k)];
        CellType cU = cellType_[cidx(i, j,   k)];
        if (cD == CellType::SOLID || cU == CellType::SOLID) {
            v_[vidx(i,j,k)] = 0.0f;
            continue;
        }
        if (cD == CellType::AIR && cU == CellType::AIR) continue;
        float pD = (cD == CellType::FLUID) ? pressure_[cidx(i,j-1,k)] : 0.0f;
        float pU = (cU == CellType::FLUID) ? pressure_[cidx(i,j,  k)] : 0.0f;
        v_[vidx(i,j,k)] -= (pU - pD);
    }

    // w-faces: face at k is between cells (i,j,k-1) and (i,j,k)
    for (int k = 1; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        CellType cB = cellType_[cidx(i, j, k-1)];
        CellType cF = cellType_[cidx(i, j, k  )];
        if (cB == CellType::SOLID || cF == CellType::SOLID) {
            w_[widx(i,j,k)] = 0.0f;
            continue;
        }
        if (cB == CellType::AIR && cF == CellType::AIR) continue;
        float pB = (cB == CellType::FLUID) ? pressure_[cidx(i,j,k-1)] : 0.0f;
        float pF = (cF == CellType::FLUID) ? pressure_[cidx(i,j,k  )] : 0.0f;
        w_[widx(i,j,k)] -= (pF - pB);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  transferG2P — FLIP/PIC blend: update particle velocities from grid
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::transferG2P() {
    for (auto& p : particles_) {
        const float px = p.pos.x(), py = p.pos.y(), pz = p.pos.z();

        // PIC velocity from the post-projection grid
        Eigen::Vector3f vPIC(
            trilerp_u(px, py, pz),
            trilerp_v(px, py, pz),
            trilerp_w(px, py, pz));

        // FLIP delta from (post - pre) projection grid
        // Temporarily subtract old from current to compute delta inline
        // We'll use a trick: pass old arrays through trilerp with swapped data.
        // Re-use trilerp helpers by swapping pointers temporarily is messy in this
        // design; instead compute delta as (new - old) at the particle position.
        // This requires two trilerps per component — acceptable at 2600 particles.
        auto lerp_uOld = [&](float x, float y, float z) {
            // Same formula as trilerp_u but over uOld_
            float fx = x, fy = y - 0.5f, fz = z - 0.5f;
            int i0 = static_cast<int>(std::floor(fx));
            int j0 = static_cast<int>(std::floor(fy));
            int k0 = static_cast<int>(std::floor(fz));
            float tx = fx-i0, ty = fy-j0, tz = fz-k0;
            int i1 = clampi(i0+1,0,SIM_N); i0 = clampi(i0,0,SIM_N);
            int j1 = clampi(j0+1,0,SIM_N-1); j0 = clampi(j0,0,SIM_N-1);
            int k1 = clampi(k0+1,0,SIM_N-1); k0 = clampi(k0,0,SIM_N-1);
            return (1-tx)*(1-ty)*(1-tz)*uOld_[uidx(i0,j0,k0)]
                  +tx    *(1-ty)*(1-tz)*uOld_[uidx(i1,j0,k0)]
                  +(1-tx)*ty    *(1-tz)*uOld_[uidx(i0,j1,k0)]
                  +tx    *ty    *(1-tz)*uOld_[uidx(i1,j1,k0)]
                  +(1-tx)*(1-ty)*tz    *uOld_[uidx(i0,j0,k1)]
                  +tx    *(1-ty)*tz    *uOld_[uidx(i1,j0,k1)]
                  +(1-tx)*ty    *tz    *uOld_[uidx(i0,j1,k1)]
                  +tx    *ty    *tz    *uOld_[uidx(i1,j1,k1)];
        };

        auto lerp_vOld = [&](float x, float y, float z) {
            float fx = x-0.5f, fy = y, fz = z-0.5f;
            int i0=static_cast<int>(std::floor(fx));
            int j0=static_cast<int>(std::floor(fy));
            int k0=static_cast<int>(std::floor(fz));
            float tx=fx-i0,ty=fy-j0,tz=fz-k0;
            int i1=clampi(i0+1,0,SIM_N-1); i0=clampi(i0,0,SIM_N-1);
            int j1=clampi(j0+1,0,SIM_N);   j0=clampi(j0,0,SIM_N);
            int k1=clampi(k0+1,0,SIM_N-1); k0=clampi(k0,0,SIM_N-1);
            return (1-tx)*(1-ty)*(1-tz)*vOld_[vidx(i0,j0,k0)]
                  +tx    *(1-ty)*(1-tz)*vOld_[vidx(i1,j0,k0)]
                  +(1-tx)*ty    *(1-tz)*vOld_[vidx(i0,j1,k0)]
                  +tx    *ty    *(1-tz)*vOld_[vidx(i1,j1,k0)]
                  +(1-tx)*(1-ty)*tz    *vOld_[vidx(i0,j0,k1)]
                  +tx    *(1-ty)*tz    *vOld_[vidx(i1,j0,k1)]
                  +(1-tx)*ty    *tz    *vOld_[vidx(i0,j1,k1)]
                  +tx    *ty    *tz    *vOld_[vidx(i1,j1,k1)];
        };

        auto lerp_wOld = [&](float x, float y, float z) {
            float fx=x-0.5f,fy=y-0.5f,fz=z;
            int i0=static_cast<int>(std::floor(fx));
            int j0=static_cast<int>(std::floor(fy));
            int k0=static_cast<int>(std::floor(fz));
            float tx=fx-i0,ty=fy-j0,tz=fz-k0;
            int i1=clampi(i0+1,0,SIM_N-1); i0=clampi(i0,0,SIM_N-1);
            int j1=clampi(j0+1,0,SIM_N-1); j0=clampi(j0,0,SIM_N-1);
            int k1=clampi(k0+1,0,SIM_N);   k0=clampi(k0,0,SIM_N);
            return (1-tx)*(1-ty)*(1-tz)*wOld_[widx(i0,j0,k0)]
                  +tx    *(1-ty)*(1-tz)*wOld_[widx(i1,j0,k0)]
                  +(1-tx)*ty    *(1-tz)*wOld_[widx(i0,j1,k0)]
                  +tx    *ty    *(1-tz)*wOld_[widx(i1,j1,k0)]
                  +(1-tx)*(1-ty)*tz    *wOld_[widx(i0,j0,k1)]
                  +tx    *(1-ty)*tz    *wOld_[widx(i1,j0,k1)]
                  +(1-tx)*ty    *tz    *wOld_[widx(i0,j1,k1)]
                  +tx    *ty    *tz    *wOld_[widx(i1,j1,k1)];
        };

        Eigen::Vector3f dVel(
            vPIC.x() - lerp_uOld(px, py, pz),
            vPIC.y() - lerp_vOld(px, py, pz),
            vPIC.z() - lerp_wOld(px, py, pz));

        // FLIP/PIC blend: α=flipBlend_ → mostly FLIP (energetic), small PIC fraction damps
        p.vel = (1.0f - flipBlend_) * vPIC + flipBlend_ * (p.vel + dVel);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  advectParticles — explicit Euler + wall clamping
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::advectParticles(float dt) {
    static constexpr float EPS = 0.01f;
    static constexpr float LO  = 1.0f + EPS;
    static constexpr float HI  = static_cast<float>(SIM_N - 1) - EPS;

    for (auto& p : particles_) {
        p.pos += p.vel * dt;

        // Clamp and kill velocity component on contact
        if (p.pos.x() < LO) { p.pos.x() = LO; if (p.vel.x() < 0) p.vel.x() = 0; }
        if (p.pos.x() > HI) { p.pos.x() = HI; if (p.vel.x() > 0) p.vel.x() = 0; }
        if (p.pos.y() < LO) { p.pos.y() = LO; if (p.vel.y() < 0) p.vel.y() = 0; }
        if (p.pos.y() > HI) { p.pos.y() = HI; if (p.vel.y() > 0) p.vel.y() = 0; }
        if (p.pos.z() < LO) { p.pos.z() = LO; if (p.vel.z() < 0) p.vel.z() = 0; }
        if (p.pos.z() > HI) { p.pos.z() = HI; if (p.vel.z() > 0) p.vel.z() = 0; }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  update — one complete PIC/FLIP tick
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::update(float dt) {
    // Clamp dt to avoid instability on first frame or hiccups
    dt = std::min(dt, 1.0f / 30.0f);

    if (!profile_) {
        classifyCells();
        transferP2G();
        saveOldVelocities();
        addGravity(dt);
        enforceBoundary();
        computeDivergence();
        pressureSolve();
        velocityProject();
        enforceBoundary();
        transferG2P();
        advectParticles(dt);
        return;
    }

    // ── Profiled path: per-phase EMA (α=0.1), print summary every 60 frames ──
    using Clk = std::chrono::steady_clock;
    auto t0 = Clk::now();
    auto tick = [&](double& accum) {
        const auto now = Clk::now();
        const double us = std::chrono::duration<double, std::micro>(now - t0).count();
        constexpr double a = 0.1;
        accum = (1.0 - a) * accum + a * us;
        t0 = now;
    };

    classifyCells();       tick(profClassify_);
    transferP2G();         tick(profP2G_);
    saveOldVelocities();   tick(profSave_);
    addGravity(dt);        tick(profGrav_);
    enforceBoundary();     tick(profBdy_);
    computeDivergence();   tick(profDiv_);
    pressureSolve();       tick(profPressure_);
    velocityProject();     tick(profProj_);
    enforceBoundary();     tick(profBdy_);  // fold both boundary passes into one counter
    transferG2P();         tick(profG2P_);
    advectParticles(dt);   tick(profAdvect_);

    if (++profileFrame_ % 60 == 0) {
        const double phys =
            profClassify_ + profP2G_      + profSave_ + profGrav_ +
            profBdy_      + profDiv_      + profPressure_ + profProj_ +
            profG2P_      + profAdvect_;
        std::cerr << std::fixed << std::setprecision(2)
                  << "[FluidEngine] phys=" << phys / 1000.0 << "ms"
                  << "  cls="   << profClassify_  / 1000.0
                  << "  p2g="   << profP2G_       / 1000.0
                  << "  sav="   << profSave_      / 1000.0
                  << "  grv="   << profGrav_      / 1000.0
                  << "  bdy="   << profBdy_       / 1000.0
                  << "  div="   << profDiv_       / 1000.0
                  << "  pres="  << profPressure_  / 1000.0
                  << " (" << pressureIter_ << "it/ω=" << sorOmega_ << ")"
                  << "  prj="   << profProj_      / 1000.0
                  << "  g2p="   << profG2P_       / 1000.0
                  << "  adv="   << profAdvect_    / 1000.0
                  << "  render=" << profRender_   / 1000.0
                  << "ms  parts=" << particles_.size()
                  << "\n";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderSurface — particle splat at full 64×64 pixel resolution per face
//
//  The physics grid is 20³, but each Particle::pos is a continuous float.
//  Rendering at pixel resolution uses the continuous positions: every
//  particle close to a face is splatted with a small kernel into that face's
//  64×64 density buffer.  Water thus moves pixel-by-pixel instead of
//  snapping to sim-cell (3×3) blocks.
//
//  Face index order: 0:x-min 1:x-max 2:y-min 3:y-max 4:z-min 5:z-max.
//
//  Depth shading is still computed per rendered pixel from
//  (pixelPos − fluidCentroid) · gravityUnit, using the centroid cached by
//  classifyCells().
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::renderSurface(const PixelCb& setPixel) {
    using Clk = std::chrono::steady_clock;
    const auto tRenderStart = Clk::now();

    // ── Clear the 6 per-face density buffers ──
    std::fill(faceBuf_.begin(), faceBuf_.end(), 0.0f);

    // ── Splat each particle onto the faces it is close to ──
    // splatRange: a particle farther than this from a face contributes nothing
    //             to that face's pixel buffer (sim-cell units).
    // kernelR:    radius of the 2-D splat kernel in render pixels.  At
    //             SIM_TO_RENDER ≈ 3.56, one sim cell ≈ 3.56 px; R=4 gives
    //             each particle a ~9x9 pixel footprint.
    constexpr float splatRange    = 2.5f;
    constexpr int   kernelR       = 4;
    constexpr float kernelInvR2   = 1.0f / static_cast<float>(kernelR * kernelR);
    constexpr float invSplatRange = 1.0f / splatRange;

    const float nearFace = 1.0f;
    const float farFace  = static_cast<float>(SIM_N - 1);

    for (const auto& p : particles_) {
        const float px = p.pos.x();
        const float py = p.pos.y();
        const float pz = p.pos.z();

        const float dToFace[6] = {
            px - nearFace,   // 0: x-min
            farFace - px,    // 1: x-max
            py - nearFace,   // 2: y-min
            farFace - py,    // 3: y-max
            pz - nearFace,   // 4: z-min
            farFace - pz     // 5: z-max
        };

        for (int f = 0; f < 6; ++f) {
            const float dFace = dToFace[f];
            if (dFace >= splatRange || dFace <= 0.0f) continue;
            const float faceW = 1.0f - dFace * invSplatRange;

            // Free-axis particle coordinates (sim) for this face.
            float sa, sb;
            switch (f) {
                case 0: case 1: sa = py; sb = pz; break;  // x-faces: (y, z)
                case 2: case 3: sa = px; sb = pz; break;  // y-faces: (x, z)
                default:        sa = px; sb = py; break;  // z-faces: (x, y)
            }

            // Continuous render-pixel position of the splat centre.
            const float ra = (sa - 1.0f) * SIM_TO_RENDER;
            const float rb = (sb - 1.0f) * SIM_TO_RENDER;
            const int   ca = static_cast<int>(ra);
            const int   cb = static_cast<int>(rb);

            float* const facePix = &faceBuf_[f * RN_SQ];

            for (int dv = -kernelR; dv <= kernelR; ++dv) {
                const int pv = cb + dv;
                if (pv < 0 || pv >= RN) continue;
                for (int du = -kernelR; du <= kernelR; ++du) {
                    const int pu = ca + du;
                    if (pu < 0 || pu >= RN) continue;
                    const float fa = static_cast<float>(pu) + 0.5f - ra;
                    const float fb = static_cast<float>(pv) + 0.5f - rb;
                    const float d2 = fa*fa + fb*fb;
                    const float kw = 1.0f - d2 * kernelInvR2;
                    if (kw <= 0.0f) continue;
                    facePix[pv * RN + pu] += faceW * kw;
                }
            }
        }
    }

    // ── Emit pixels for each face ──
    // densityThreshold:    below this, pixel is air (not emitted).
    // densitySurfaceUpper: between threshold and this, pixel is "surface"
    //                     (brighter highlight); above, interior water.
    constexpr float densityThreshold    = 0.35f;
    constexpr float densitySurfaceUpper = 1.50f;

    const Eigen::Vector3f gv(gravity_.x, gravity_.y, gravity_.z);
    const float gLen = gv.norm();
    const Eigen::Vector3f gUnit = (gLen > 1e-6f) ? (gv / gLen) : Eigen::Vector3f(0, 0, 1);

    const Eigen::Vector3f centroidRender(
        (centroid_.x() - 1.0f) * SIM_TO_RENDER,
        (centroid_.y() - 1.0f) * SIM_TO_RENDER,
        (centroid_.z() - 1.0f) * SIM_TO_RENDER);
    const float maxDepth = static_cast<float>(RN);

    auto emitPixel = [&](int rx, int ry, int rz, float density) {
        const Eigen::Vector3f rpos(rx + 0.5f, ry + 0.5f, rz + 0.5f);
        const float depth = (rpos - centroidRender).dot(gUnit);
        const float t     = depth / maxDepth;
        const bool  surf  = density < densitySurfaceUpper;
        setPixel(rx, ry, rz, shadeWater(t, surf));
    };

    // Face 0: x-min (rx = 0), free axes (u, v) = (y, z)
    {
        const float* facePix = &faceBuf_[0 * RN_SQ];
        for (int v = 0; v < RN; ++v)
        for (int u = 0; u < RN; ++u) {
            const float d = facePix[v * RN + u];
            if (d >= densityThreshold) emitPixel(0, u, v, d);
        }
    }
    // Face 1: x-max (rx = RMAX), free axes (u, v) = (y, z)
    {
        const float* facePix = &faceBuf_[1 * RN_SQ];
        for (int v = 0; v < RN; ++v)
        for (int u = 0; u < RN; ++u) {
            const float d = facePix[v * RN + u];
            if (d >= densityThreshold) emitPixel(RMAX, u, v, d);
        }
    }
    // Face 2: y-min (ry = 0), free axes (u, v) = (x, z)
    {
        const float* facePix = &faceBuf_[2 * RN_SQ];
        for (int v = 0; v < RN; ++v)
        for (int u = 0; u < RN; ++u) {
            const float d = facePix[v * RN + u];
            if (d >= densityThreshold) emitPixel(u, 0, v, d);
        }
    }
    // Face 3: y-max (ry = RMAX)
    {
        const float* facePix = &faceBuf_[3 * RN_SQ];
        for (int v = 0; v < RN; ++v)
        for (int u = 0; u < RN; ++u) {
            const float d = facePix[v * RN + u];
            if (d >= densityThreshold) emitPixel(u, RMAX, v, d);
        }
    }
    // Face 4: z-min (rz = 0), free axes (u, v) = (x, y)
    {
        const float* facePix = &faceBuf_[4 * RN_SQ];
        for (int v = 0; v < RN; ++v)
        for (int u = 0; u < RN; ++u) {
            const float d = facePix[v * RN + u];
            if (d >= densityThreshold) emitPixel(u, v, 0, d);
        }
    }
    // Face 5: z-max (rz = RMAX)
    {
        const float* facePix = &faceBuf_[5 * RN_SQ];
        for (int v = 0; v < RN; ++v)
        for (int u = 0; u < RN; ++u) {
            const float d = facePix[v * RN + u];
            if (d >= densityThreshold) emitPixel(u, v, RMAX, d);
        }
    }

    if (profile_) {
        const double us = std::chrono::duration<double, std::micro>(
            Clk::now() - tRenderStart).count();
        constexpr double a = 0.1;
        profRender_ = (1.0 - a) * profRender_ + a * us;
    }
}
