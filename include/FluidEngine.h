#pragma once

#include "Gravity.h"

#include <Eigen/Core>
#include <cstdint>
#include <functional>
#include <vector>

// ──────────────────────────────────────────────────────────────────────────
//  FluidEngine
//
//  PIC/FLIP fluid simulation on a 20³ staggered MAC grid with ~2 particles
//  per cell.  The simulation grid is deliberately coarser than the 64³ LED
//  cube because only the six outer faces are ever rendered — the interior is
//  invisible.  Particles carry continuous position/velocity, so they raster
//  crisply onto the 64³ outer shell regardless of physics resolution.
//
//  Coordinate system
//  ─────────────────
//    Particles live in [1, SIM_N-1]³ (inside the solid outer layer).
//    Gravity is a normalised unit vector (from ImuOrientation or KeyboardTilt)
//    scaled by gravityMagnitude (cells/s²).
//
//  Per-frame API (called from WaterCube::loop)
//  ────────────────────────────────────────────
//    setGravity(g)       — store gravity direction (continuous float vector)
//    update(dt)          — advance one physics tick
//    renderSurface(vg)   — write water colour to the 6 outer faces of vg
// ──────────────────────────────────────────────────────────────────────────
class FluidEngine {
public:
    static constexpr int SIM_N  = 20;
    static constexpr int SIM_N1 = SIM_N + 1;

    // Render-surface resolution (must match VoxelGrid GRID_SIZE)
    static constexpr int RN     = 64;
    static constexpr int RN_SQ  = RN * RN;

    // Staggered MAC face array sizes
    static constexpr int U_SIZE = SIM_N1 * SIM_N  * SIM_N;   // 8 400
    static constexpr int V_SIZE = SIM_N  * SIM_N1 * SIM_N;   // 8 400
    static constexpr int W_SIZE = SIM_N  * SIM_N  * SIM_N1;  // 8 400
    static constexpr int C_SIZE = SIM_N  * SIM_N  * SIM_N;   // 8 000 centres

    // Particles are pre-reserved at construction; no runtime allocation
    static constexpr int MAX_PARTICLES = C_SIZE * 2;  // 16 000

    FluidEngine();

    // ── Configuration ──────────────────────────────────────────────────────
    void setGravity(const Gravity& g) noexcept;
    void setGravityMagnitude(float s) noexcept { gravMag_ = s; }
    void setFlipBlend(float a)        noexcept { flipBlend_ = a; }
    void setPressureIterations(int n) noexcept { pressureIter_ = n; }
    void setSORRelaxation(float w)    noexcept { sorOmega_ = w; }
    void setProfile(bool on)          noexcept { profile_ = on; }

    // Re-pour a flat slab (fillLevel ∈ [0,1]) perpendicular to current gravity
    void refill(float fillLevel);

    // ── Per-frame ──────────────────────────────────────────────────────────
    void update(float dt);

    // Write water colour to the 6 outer faces via a pixel callback.
    // The callback receives (x, y, z, 0x00RRGGBB).  The caller must
    // clear/fade the canvas before calling this.
    //
    // Pixels are produced at full 64×64 resolution per face by splatting each
    // continuous particle position onto per-face density buffers — this is
    // what lets the water move sub-sim-cell (pixel-by-pixel) even though the
    // physics grid is 20³.
    using PixelCb = std::function<void(int, int, int, uint32_t)>;
    void renderSurface(const PixelCb& setPixel);

    [[nodiscard]] std::size_t particleCount() const noexcept { return particles_.size(); }

private:
    struct Particle {
        Eigen::Vector3f pos;  // cells, [1, SIM_N-1]
        Eigen::Vector3f vel;  // cells/s
    };

    enum class CellType : uint8_t { AIR = 0, FLUID = 1, SOLID = 2 };

    // ── Index helpers (inline, zero-overhead) ──────────────────────────────
    static constexpr int cidx(int i, int j, int k) noexcept {
        return i + SIM_N * j + SIM_N * SIM_N * k;
    }
    // u-face (i,j,k): i∈[0,N], j∈[0,N-1], k∈[0,N-1]
    static constexpr int uidx(int i, int j, int k) noexcept {
        return i + SIM_N1 * j + SIM_N1 * SIM_N * k;
    }
    // v-face (i,j,k): i∈[0,N-1], j∈[0,N], k∈[0,N-1]
    static constexpr int vidx(int i, int j, int k) noexcept {
        return i + SIM_N * j + SIM_N * SIM_N1 * k;
    }
    // w-face (i,j,k): i∈[0,N-1], j∈[0,N-1], k∈[0,N]
    static constexpr int widx(int i, int j, int k) noexcept {
        return i + SIM_N * j + SIM_N * SIM_N * k;
    }

    // ── Trilinear interpolation helpers ────────────────────────────────────
    [[nodiscard]] float trilerp_u(float px, float py, float pz) const noexcept;
    [[nodiscard]] float trilerp_v(float px, float py, float pz) const noexcept;
    [[nodiscard]] float trilerp_w(float px, float py, float pz) const noexcept;

    // ── Simulation steps ───────────────────────────────────────────────────
    void classifyCells();
    void transferP2G();
    void saveOldVelocities();
    void addGravity(float dt);
    void enforceBoundary();
    void computeDivergence();
    void pressureSolve();
    void velocityProject();
    void transferG2P();
    void advectParticles(float dt);

    // ── Simulation state ───────────────────────────────────────────────────
    std::vector<Particle>   particles_;

    std::vector<float>      u_, v_, w_;              // MAC face velocities
    std::vector<float>      uOld_, vOld_, wOld_;     // pre-projection snapshot
    std::vector<float>      uW_,  vW_,  wW_;         // P2G accumulation weights
    std::vector<float>      pressure_;               // in-place SOR
    std::vector<float>      divergence_;
    std::vector<CellType>   cellType_;

    // Cached per-frame aggregates (computed inside classifyCells)
    Eigen::Vector3f         centroid_  = Eigen::Vector3f::Zero();  // sim-cell units
    int                     fluidCount_ = 0;

    // Per-face pixel splat buffers for rendering (RN×RN each, 6 faces)
    // Indexed [face * RN_SQ + pixel]. Face order:
    //   0: x-min, 1: x-max, 2: y-min, 3: y-max, 4: z-min, 5: z-max
    std::vector<float> faceBuf_;   // size 6 * RN_SQ

    Gravity  gravity_;
    float    gravMag_      = 25.0f;   // cells/s²
    float    flipBlend_    = 0.90f;   // 0 = pure PIC, 1 = pure FLIP
    int      pressureIter_ = 40;      // Red-Black SOR sweeps (warm-start; 40 suffices)
    float    sorOmega_     = 1.7f;    // SOR relaxation factor

    // Profiling
    bool     profile_       = false;
    uint64_t profileFrame_  = 0;
    double   profClassify_  = 0, profP2G_    = 0, profSave_  = 0;
    double   profGrav_      = 0, profBdy_    = 0, profDiv_   = 0;
    double   profPressure_  = 0, profProj_   = 0, profG2P_   = 0;
    double   profAdvect_    = 0, profRender_ = 0;
};
