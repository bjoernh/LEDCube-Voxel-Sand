#pragma once

#include <array>
#include <cstdint>

#include "SandEngine.h"

// ──────────────────────────────────────────────────────────────────────────
//  SandSpawner
//
//  "Pours" grains into the cube every spawnEveryN frames. The spawn face
//  is the cube face whose outward normal is most opposite to the current
//  gravity vector — so when the cube is tilted, new grains always enter
//  from the face that is physically "up" and fall naturally into the heap.
//
//  A Wang-hash spread across the two free axes prevents single-column
//  stacking and produces a more natural pile.
//
//  Usage:
//    SandSpawner spawner;                          // defaults
//    spawner.tick(engine, frame, currentGravity);  // call once per frame
// ──────────────────────────────────────────────────────────────────────────

class SandSpawner {
public:
    static constexpr int DEFAULT_EVERY_N = 2;
    static constexpr int DEFAULT_BURST   = 25;

    // Warm gold/ochre sand colours (0x00RRGGBB).
    static constexpr std::array<uint32_t, 8> DEFAULT_PALETTE = {
        0xD4AF37u, 0xC2A050u, 0xE8C870u, 0xB8935Au,
        0xF0D060u, 0xA08040u, 0xCC9944u, 0xE0B855u,
    };

    explicit SandSpawner(int spawnEveryN = DEFAULT_EVERY_N,
                         int burst       = DEFAULT_BURST);

    /// Call once per frame. Spawns a burst when (frame % spawnEveryN_) == 0,
    /// entering from the face opposite the supplied gravity vector.
    void tick(SandEngine& engine, int frame, const Gravity& gravity);

    // Runtime-adjustable parameters (exposed for live tweaking via the
    // matrixserver AnimationParams system).
    void setSpawnEveryN(int n) noexcept { spawnEveryN_ = (n > 0) ? n : 1; }
    void setBurst(int b)       noexcept { burst_ = (b >= 0) ? b : 0; }

    [[nodiscard]] int spawnEveryN() const noexcept { return spawnEveryN_; }
    [[nodiscard]] int burst()       const noexcept { return burst_; }

private:
    int spawnEveryN_;
    int burst_;
};
