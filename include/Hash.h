#pragma once

#include <cstdint>

// ──────────────────────────────────────────────────────────────────────────
//  Wang hash  (avalanche integer hash)
//
//  Used wherever a cheap, bias-free pseudo-random integer is needed:
//    • SandEngine: per-particle starting index in the slide-direction table
//    • SandSpawner: per-grain x-position spread across the spawn row
//
//  Primes follow the standard Wang/Jenkins construction.
// ──────────────────────────────────────────────────────────────────────────

[[nodiscard]] inline uint32_t wangHash(uint32_t v) noexcept {
    v = (v ^ 61u) ^ (v >> 16u);
    v *= 9u;
    v ^= v >> 4u;
    v *= 0x27d4eb2du;
    v ^= v >> 15u;
    return v;
}
