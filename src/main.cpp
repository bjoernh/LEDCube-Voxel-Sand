#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

#include <led-matrix.h>

#include "SandEngine.h"
#include "SandSpawner.h"
#include "FaceMapper.h"

using namespace rgb_matrix;
using Clock = std::chrono::steady_clock;
using TP    = std::chrono::time_point<Clock>;

// ──────────────────────────────────────────────────────────────────────────
//  Configuration
// ──────────────────────────────────────────────────────────────────────────

static constexpr int TARGET_FPS = 100;

// ──────────────────────────────────────────────────────────────────────────
//  Graceful shutdown
// ──────────────────────────────────────────────────────────────────────────

static volatile bool g_running = true;

static void onSignal(int /*sig*/) {
    g_running = false;
}

// ──────────────────────────────────────────────────────────────────────────
//  Matrix initialisation
// ──────────────────────────────────────────────────────────────────────────

static RGBMatrix* createMatrix() {
    RGBMatrix::Options opts;
    opts.hardware_mapping   = "adafruit-hat-pwm";
    opts.rows               = 64;           // pixels per panel height
    opts.cols               = 64;           // pixels per panel width
    opts.chain_length       = 6;            // panels chained per parallel strand
    opts.parallel           = 1;            // 3 parallel strands → 6 panels total
    opts.brightness         = 50;           // 0–100 %
    opts.show_refresh_rate  = false;
    opts.pwm_lsb_nanoseconds = 130;
    opts.led_rgb_sequence = "rgb";


    RuntimeOptions rt;
    rt.gpio_slowdown = 4;   // Raspberry Pi 4; reduce to 2-3 for Pi 3
    rt.do_gpio_init = true;

    RGBMatrix* m = RGBMatrix::CreateFromOptions(opts, rt);
    if (!m) {
        std::fprintf(stderr, "[SandCube] ERROR: Failed to create RGBMatrix.\n"
                             "           Check wiring and run as root (sudo).\n");
    }
    return m;
}

// ──────────────────────────────────────────────────────────────────────────
//  Main loop
// ──────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    const bool testMode = (argc >= 2 && std::string(argv[1]) == "--test");

    // ── Hardware init ────────────────────────────────────────────────────
    RGBMatrix* matrix = createMatrix();
    if (!matrix) return 1;

    FrameCanvas* canvas = matrix->CreateFrameCanvas();

    // ── Test-pattern mode ────────────────────────────────────────────────
    if (testMode) {
        std::printf("[SandCube] Test pattern — press Ctrl-C to quit.\n"
                    "  Each panel shows a unique colour.\n"
                    "  White edges mark physical top (row) and left (col).\n"
                    "  Yellow pixel marks top-left origin (lx=0, ly=0).\n");
        // Need a dummy grid for the mapper in test mode
        VoxelGrid dummyGrid;
        FaceMapper testMapper(dummyGrid);
        testMapper.renderTestPattern(canvas);
        canvas = matrix->SwapOnVSync(canvas);
        while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100));

        canvas->Clear();
        matrix->SwapOnVSync(canvas);
        delete matrix;
        return 0;
    }

    // ── Simulation init ──────────────────────────────────────────────────
    SandEngine engine;
    engine.setGravity({0, -1, 0});   // gravity pointing down (-y)

    FaceMapper  mapper(engine.getGrid());
    SandSpawner spawner;

    // ── Frame-rate cap setup ─────────────────────────────────────────────
    static constexpr auto FRAME_DUR =
        std::chrono::microseconds(1'000'000 / TARGET_FPS);   // ≈ 16 667 µs

    TP nextFrame = Clock::now() + FRAME_DUR;
    int frame    = 0;

    std::printf("[SandCube] Running at %d FPS — press Ctrl-C to quit.\n",
                TARGET_FPS);

    // ── Main loop ────────────────────────────────────────────────────────
    while (g_running) {

        // 1. Spawn new sand grains.
        spawner.tick(engine, frame);

        // 2. Advance physics one tick.
        engine.update();

        // 3. Map the 3-D surface to the 6 LED panels and swap buffer.
        mapper.renderToPanels(canvas);
        canvas = matrix->SwapOnVSync(canvas, /*vsync_multiple=*/1);

        // 4. Cap at TARGET_FPS.
        //    sleep_until absorbs render jitter; if we're already late it
        //    returns immediately without accumulating debt.
        std::this_thread::sleep_until(nextFrame);
        nextFrame += FRAME_DUR;
        ++frame;
    }

    // ── Clean shutdown ───────────────────────────────────────────────────
    std::printf("\n[SandCube] Shutting down.\n");
    canvas->Clear();
    matrix->SwapOnVSync(canvas);
    delete matrix;
    return 0;
}
