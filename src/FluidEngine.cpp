#include "FluidEngine.h"
#include "VoxelGrid.h"   // for GRID_SIZE constant

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>

// ─── Render-grid size (from VoxelGrid.h constants) ──────────────────────────
static constexpr int RN    = GRID_SIZE;   // 64
static constexpr int RMAX  = RN - 1;      // 63

// Mapping sim-cells → render voxels (e.g. sim cell 10 → render voxel 32)
static constexpr float S2R = static_cast<float>(RN) / static_cast<float>(FluidEngine::SIM_N);

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
    , pressure_    (C_SIZE, 0.0f)
    , pressureNext_(C_SIZE, 0.0f)
    , divergence_  (C_SIZE, 0.0f)
    , cellType_    (C_SIZE, CellType::AIR)
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
//  classifyCells — rebuild cellType_ from particle positions each tick
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
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j) {
        u_[uidx(0,    j, k)] = 0.0f;
        u_[uidx(SIM_N,j, k)] = 0.0f;
    }
    for (int k = 0; k < SIM_N; ++k)
    for (int i = 0; i < SIM_N; ++i) {
        v_[vidx(i, 0,     k)] = 0.0f;
        v_[vidx(i, SIM_N, k)] = 0.0f;
    }
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        w_[widx(i, j, 0    )] = 0.0f;
        w_[widx(i, j, SIM_N)] = 0.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  computeDivergence — finite-difference divergence for FLUID cells
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::computeDivergence() {
    std::fill(divergence_.begin(), divergence_.end(), 0.0f);
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        if (cellType_[cidx(i,j,k)] != CellType::FLUID) continue;
        divergence_[cidx(i,j,k)] =
              (u_[uidx(i+1,j,k)] - u_[uidx(i,j,k)])
            + (v_[vidx(i,j+1,k)] - v_[vidx(i,j,k)])
            + (w_[widx(i,j,k+1)] - w_[widx(i,j,k)]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  pressureSolve — Jacobi iterations
//
//  Boundary conditions:
//    AIR neighbours → Dirichlet p=0 (pressure_ is always 0 for AIR cells)
//    SOLID neighbours → Neumann: excluded from the stencil (not counted)
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::pressureSolve() {
    std::fill(pressure_.begin(),     pressure_.end(),     0.0f);
    std::fill(pressureNext_.begin(), pressureNext_.end(), 0.0f);

    static const int di[6] = {-1, 1,  0, 0,  0, 0};
    static const int dj[6] = { 0, 0, -1, 1,  0, 0};
    static const int dk[6] = { 0, 0,  0, 0, -1, 1};

    for (int iter = 0; iter < jacobiIter_; ++iter) {
        for (int k = 0; k < SIM_N; ++k)
        for (int j = 0; j < SIM_N; ++j)
        for (int i = 0; i < SIM_N; ++i) {
            if (cellType_[cidx(i,j,k)] != CellType::FLUID) continue;

            float sum   = 0.0f;
            int   count = 0;

            for (int f = 0; f < 6; ++f) {
                int ni = i + di[f], nj = j + dj[f], nk = k + dk[f];
                if (ni < 0 || ni >= SIM_N || nj < 0 || nj >= SIM_N || nk < 0 || nk >= SIM_N)
                    continue;  // out-of-bounds = solid wall → Neumann, skip
                if (cellType_[cidx(ni,nj,nk)] == CellType::SOLID) continue;
                // AIR: pressure_[n] = 0, still counts in denominator (Dirichlet)
                sum += pressure_[cidx(ni,nj,nk)];
                ++count;
            }

            if (count > 0) {
                pressureNext_[cidx(i,j,k)] =
                    (sum - divergence_[cidx(i,j,k)]) / static_cast<float>(count);
            }
        }

        std::swap(pressure_, pressureNext_);
        // AIR and SOLID cells must remain 0 — maintained since we only write FLUID cells above
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

    classifyCells();
    transferP2G();
    saveOldVelocities();
    addGravity(dt);
    enforceBoundary();
    computeDivergence();
    pressureSolve();
    velocityProject();
    enforceBoundary();  // re-apply after projection
    transferG2P();
    advectParticles(dt);
}

// ─────────────────────────────────────────────────────────────────────────────
//  renderSurface
//
//  Only the 6 outer faces of the 64³ LED grid are visible.  For each outer
//  face voxel we look up the cell in the sim (or the innermost fluid-capable
//  layer for wall-adjacent voxels) and shade it if it is FLUID.
//
//  Depth is measured along the gravity axis from the centroid of all FLUID
//  cell centres.  t=0 (shallow/bright) is at the free surface, t=1 (dark) at
//  the farthest-down voxel.
// ─────────────────────────────────────────────────────────────────────────────
void FluidEngine::renderSurface(const PixelCb& setPixel) const {
    // ── Compute surface centroid in render coordinates ────────────────────
    Eigen::Vector3f centroid = Eigen::Vector3f::Zero();
    int fluidCount = 0;
    for (int k = 0; k < SIM_N; ++k)
    for (int j = 0; j < SIM_N; ++j)
    for (int i = 0; i < SIM_N; ++i) {
        if (cellType_[cidx(i,j,k)] == CellType::FLUID) {
            centroid += Eigen::Vector3f(
                (i + 0.5f) * S2R,
                (j + 0.5f) * S2R,
                (k + 0.5f) * S2R);
            ++fluidCount;
        }
    }
    if (fluidCount > 0) centroid /= static_cast<float>(fluidCount);

    // Gravity unit vector for depth projection
    const Eigen::Vector3f gv(gravity_.x, gravity_.y, gravity_.z);
    const float gLen = gv.norm();
    const Eigen::Vector3f gUnit = (gLen > 1e-6f) ? gv / gLen : Eigen::Vector3f(0,-1,0);

    // Normalisation: deepest possible voxel is ~RN cells from centroid along gUnit
    const float maxDepth = static_cast<float>(RN);

    // Helper: is a sim cell adjacent to an AIR cell? (→ surface highlight)
    auto isSurfaceCell = [&](int ci, int cj, int ck) -> bool {
        static const int di[6]={-1,1,0,0,0,0};
        static const int dj[6]={0,0,-1,1,0,0};
        static const int dk[6]={0,0,0,0,-1,1};
        for (int f = 0; f < 6; ++f) {
            int ni=ci+di[f], nj=cj+dj[f], nk=ck+dk[f];
            if (ni < 0 || ni >= SIM_N || nj < 0 || nj >= SIM_N || nk < 0 || nk >= SIM_N)
                continue;
            if (cellType_[cidx(ni,nj,nk)] == CellType::AIR) return true;
        }
        return false;
    };

    // Helper: shade a render voxel given sim-cell indices
    auto place = [&](int rx, int ry, int rz, int ci, int cj, int ck) {
        if (cellType_[cidx(ci,cj,ck)] != CellType::FLUID) return;
        Eigen::Vector3f rpos(rx + 0.5f, ry + 0.5f, rz + 0.5f);
        float depth = (rpos - centroid).dot(gUnit);
        float t     = depth / maxDepth;
        bool  surf  = isSurfaceCell(ci, cj, ck);
        setPixel(rx, ry, rz, shadeWater(t, surf));
    };

    // Sim cell index for a given render coordinate clamped to interior.
    // The outer render faces map to the sim cells just inside the solid layer.
    auto simI = [](int r) -> int {
        return clampi(static_cast<int>(r * FluidEngine::SIM_N / RN), 1, SIM_N - 2);
    };

    // ── Top (ry = RMAX) and bottom (ry = 0) ─────────────────────────────
    for (int rz = 0; rz <= RMAX; ++rz)
    for (int rx = 0; rx <= RMAX; ++rx) {
        int ci = simI(rx), ck = simI(rz);
        place(rx, RMAX, rz, ci, SIM_N-2, ck);
        place(rx,    0, rz, ci,        1, ck);
    }

    // ── Front (rz = RMAX) and back (rz = 0) ─────────────────────────────
    for (int ry = 0; ry <= RMAX; ++ry)
    for (int rx = 0; rx <= RMAX; ++rx) {
        int ci = simI(rx), cj = simI(ry);
        place(rx, ry, RMAX, ci, cj, SIM_N-2);
        place(rx, ry,    0, ci, cj,        1);
    }

    // ── Left (rx = 0) and right (rx = RMAX) ─────────────────────────────
    for (int ry = 0; ry <= RMAX; ++ry)
    for (int rz = 0; rz <= RMAX; ++rz) {
        int cj = simI(ry), ck = simI(rz);
        place(   0, ry, rz,        1, cj, ck);
        place(RMAX, ry, rz, SIM_N-2, cj, ck);
    }
}
