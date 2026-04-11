#pragma once
#include "OrientationSource.h"
#include <atomic>
#include <termios.h>
#include <thread>

// ──────────────────────────────────────────────────────────────────────────
//  KeyboardTilt
//
//  Reads WASD key-presses from stdin (raw, non-blocking terminal) on a
//  background thread and adjusts a 2-axis tilt state.
//
//  Key bindings
//  ────────────
//    w / s  →  +Z / -Z tilt  (one step per press, clamped to ±1)
//    a / d  →  -X / +X tilt
//    r      →  reset to straight-down  {0, -1, 0}
//
//  Resulting gravity
//  ─────────────────
//    { tiltX, -1.0f, tiltZ }
//
//  The terminal is put into raw non-blocking mode for the lifetime of
//  this object and restored on destruction.
// ──────────────────────────────────────────────────────────────────────────
class KeyboardTilt : public OrientationSource {
public:
    KeyboardTilt();
    ~KeyboardTilt() override;

    // Non-copyable, non-movable (owns a thread and terminal state)
    KeyboardTilt(const KeyboardTilt&)            = delete;
    KeyboardTilt& operator=(const KeyboardTilt&) = delete;

    [[nodiscard]] Gravity getGravity() const noexcept override;

private:
    void inputLoop();

    std::atomic<float> tiltX_{0.0f};
    std::atomic<float> tiltZ_{0.0f};
    std::atomic<bool> running_{true};
    std::thread       thread_;
    struct termios    savedTermios_{};
    bool              termiosValid_{false};
};
