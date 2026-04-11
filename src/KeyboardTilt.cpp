#include "KeyboardTilt.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unistd.h>

// ──────────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────────

KeyboardTilt::KeyboardTilt() {
    if (tcgetattr(STDIN_FILENO, &savedTermios_) == 0) {
        termiosValid_ = true;
        struct termios raw = savedTermios_;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
        raw.c_cc[VMIN]  = 0;   // non-blocking: return immediately
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    thread_ = std::thread(&KeyboardTilt::inputLoop, this);
}

KeyboardTilt::~KeyboardTilt() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (termiosValid_) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &savedTermios_);
    }
}

Gravity KeyboardTilt::getGravity() const noexcept {
    return Gravity{
        tiltX_.load(std::memory_order_relaxed),
        -1.0f,
        tiltZ_.load(std::memory_order_relaxed)
    };
}

void KeyboardTilt::inputLoop() {
    using namespace std::chrono_literals;

    while (running_.load(std::memory_order_relaxed)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        struct timeval tv = {0, 10000}; // Wait up to 10ms for input

        if (select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                float tx = tiltX_.load(std::memory_order_relaxed);
                float tz = tiltZ_.load(std::memory_order_relaxed);

                switch (c) {
                    case 'w': tz = std::min(tz + 0.1f, 1.0f); break;
                    case 's': tz = std::max(tz - 0.1f, -1.0f); break;
                    case 'd': tx = std::min(tx + 0.1f, 1.0f); break;
                    case 'a': tx = std::max(tx - 0.1f, -1.0f); break;
                    case 'r': tx = 0.0f; tz = 0.0f; break;
                    default: break;
                }

                tiltX_.store(tx, std::memory_order_relaxed);
                tiltZ_.store(tz, std::memory_order_relaxed);

                const Gravity g = getGravity();
                std::printf("[Tilt] gravity = {%+.1f, %+.1f, %+.1f}\n",
                            g.x, g.y, g.z);
                std::fflush(stdout);
            }
        }
        std::this_thread::sleep_for(8ms);   // ~125 Hz poll, negligible CPU
    }
}
