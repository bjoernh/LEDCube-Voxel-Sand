#pragma once
#include "Gravity.h"

// ──────────────────────────────────────────────────────────────────────────
//  OrientationSource
//
//  Abstract provider of a discrete gravity vector.
//  Implement this interface to feed orientation data from any source:
//  keyboard input, IMU (MPU-6050, BNO055, …), network, etc.
//
//  The SandEngine consumes one Gravity value per tick.  The main loop
//  calls getGravity() and forwards any change to engine.setGravity().
// ──────────────────────────────────────────────────────────────────────────
class OrientationSource {
public:
    virtual ~OrientationSource() = default;

    /// Returns the current discrete gravity vector.
    /// Must be thread-safe (may be called from the main loop while the
    /// concrete implementation updates state from a background thread).
    [[nodiscard]] virtual Gravity getGravity() const noexcept = 0;
};
