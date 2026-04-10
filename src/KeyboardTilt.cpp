#include "KeyboardTilt.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unistd.h>

// ──────────────────────────────────────────────────────────────────────────

static int clamp1(int v) { return std::max(-1, std::min(1, v)); }

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
    return { tiltX_.load(std::memory_order_relaxed),
             -1,
             tiltZ_.load(std::memory_order_relaxed) };
}

void KeyboardTilt::inputLoop() {
    using namespace std::chrono_literals;

    while (running_.load(std::memory_order_relaxed)) {
        char c{};
        if (::read(STDIN_FILENO, &c, 1) == 1) {
            switch (c) {
                case 'w': tiltZ_.store(clamp1(tiltZ_.load() + 1)); break;
                case 's': tiltZ_.store(clamp1(tiltZ_.load() - 1)); break;
                case 'a': tiltX_.store(clamp1(tiltX_.load() - 1)); break;
                case 'd': tiltX_.store(clamp1(tiltX_.load() + 1)); break;
                case 'r': tiltX_.store(0); tiltZ_.store(0);        break;
                default:  break;
            }
            const Gravity g = getGravity();
            std::printf("[Tilt] gravity = {%+d, %+d, %+d}\n",
                        g.dx, g.dy, g.dz);
            std::fflush(stdout);
        }
        std::this_thread::sleep_for(8ms);   // ~125 Hz poll, negligible CPU
    }
}
