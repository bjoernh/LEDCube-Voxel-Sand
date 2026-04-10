#pragma once

#include <array>
#include <cstdint>

#include "SandEngine.h"

// ──────────────────────────────────────────────────────────────────────────
//  SandSpawner
//
//  "Pours" grains from the top-centre of the cube every spawnEveryN frames.
//  A Wang-hash spread across the full row width prevents a single tall column
//  and makes the heap look more natural.
//
//  Usage:
//    SandSpawner spawner;          // defaults: every 2 frames, 20 grains
//    spawner.tick(engine, frame);  // call once per main-loop iteration
// ──────────────────────────────────────────────────────────────────────────

class SandSpawner {
public:
    static constexpr int DEFAULT_EVERY_N = 2;
    static constexpr int DEFAULT_BURST   = 10;

    // Warm gold/ochre sand colours (0x00RRGGBB).
    static constexpr std::array<uint32_t, 8> DEFAULT_PALETTE = {
        0xD4AF37u, 0xC2A050u, 0xE8C870u, 0xB8935Au,
        0xF0D060u, 0xA08040u, 0xCC9944u, 0xE0B855u,
    };

    explicit SandSpawner(int spawnEveryN = DEFAULT_EVERY_N,
                         int burst       = DEFAULT_BURST);

    /// Call once per frame.  Spawns a burst when (frame % spawnEveryN_) == 0.
    void tick(SandEngine& engine, int frame);

private:
    int spawnEveryN_;
    int burst_;
};
