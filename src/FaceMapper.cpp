#include "FaceMapper.h"
#include <cstdio>
#include <cstring>

// ──────────────────────────────────────────────────────────────────────────
//  Panel → FrameCanvas coordinate mapping
//
//  With --led-chain=6 --led-parallel=1 (rows=64, cols=64):
//    canvas width  = 64 * 6 = 384
//    canvas height = 64 * 1 = 64
//
//  Panel index p occupies:
//    canvas_x = p * 64 + lx
//    canvas_y = ly
//
//  Per-panel rotation is loaded from panels.conf at startup.
//  Rotation is applied in writePixel before mapping to canvas coordinates.
// ──────────────────────────────────────────────────────────────────────────

// Face index constants (match physical chain order)
static constexpr int FACE_TOP    = 0;
static constexpr int FACE_BACK   = 1;
static constexpr int FACE_LEFT   = 2;
static constexpr int FACE_FRONT  = 3;
static constexpr int FACE_RIGHT  = 4;
static constexpr int FACE_BOTTOM = 5;

static constexpr const char* FACE_NAMES[6] = {
    "top", "back", "left", "front", "right", "bottom"
};

// ── Constructor / config loader ────────────────────────────────────────────

FaceMapper::FaceMapper(const VoxelGrid& grid, const char* configPath)
    : grid_(grid)
{
    loadConfig(configPath);
}

void FaceMapper::loadConfig(const char* path) {
    rotation_.fill(0);

    std::FILE* f = std::fopen(path, "r");
    if (!f) {
        std::printf("[FaceMapper] No config at '%s', using 0° rotation for all panels.\n", path);
        return;
    }

    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        // Skip comments and blank lines
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char name[32];
        int  deg = 0;
        if (std::sscanf(p, "%31s %d", name, &deg) != 2) continue;

        for (int i = 0; i < 6; ++i) {
            if (std::strcmp(name, FACE_NAMES[i]) == 0) {
                // Normalise to 0/90/180/270
                deg = ((deg % 360) + 360) % 360;
                if (deg != 0 && deg != 90 && deg != 180 && deg != 270) {
                    std::printf("[FaceMapper] WARN: invalid rotation %d for '%s', using 0.\n", deg, name);
                    deg = 0;
                }
                rotation_[i] = deg;
                break;
            }
        }
    }
    std::fclose(f);

    std::printf("[FaceMapper] Panel rotations: top=%d back=%d left=%d front=%d right=%d bottom=%d\n",
                rotation_[0], rotation_[1], rotation_[2],
                rotation_[3], rotation_[4], rotation_[5]);
}

// ── Helpers ────────────────────────────────────────────────────────────────

void FaceMapper::unpackColor(uint32_t color,
                             uint8_t& r, uint8_t& g, uint8_t& b) noexcept {
    r = static_cast<uint8_t>((color >> 16) & 0xFFu);
    g = static_cast<uint8_t>((color >>  8) & 0xFFu);
    b = static_cast<uint8_t>( color        & 0xFFu);
}

void FaceMapper::writePixel(rgb_matrix::FrameCanvas* canvas,
                            int panel, int lx, int ly, uint32_t color) const {
    // Apply clockwise rotation around panel centre
    const int S1 = GRID_SIZE - 1;
    int rx = lx, ry = ly;
    switch (rotation_[panel]) {
        case 90:  rx = S1 - ly; ry = lx;       break;
        case 180: rx = S1 - lx; ry = S1 - ly;  break;
        case 270: rx = ly;      ry = S1 - lx;  break;
        default:  break;  // 0°: no change
    }

    uint8_t r, g, b;
    unpackColor(color, r, g, b);
    canvas->SetPixel(panel * GRID_SIZE + rx, ry, r, g, b);
}

// ── Main render ────────────────────────────────────────────────────────────

void FaceMapper::renderToPanels(rgb_matrix::FrameCanvas* canvas) const {
    canvas->Clear();

    static constexpr int S  = GRID_SIZE;
    static constexpr int S1 = GRID_SIZE - 1;   // 63

    // Physical chain order: 0=Top, 1=Back, 2=Left, 3=Front, 4=Right, 5=Bottom

    // ── Panel 0 — Top face  (y = 63) ───────────────────────────────────────
    //   lx = x, ly = z
    for (int z = 0; z < S; ++z)
    for (int x = 0; x < S; ++x) {
        const uint32_t c = grid_.getCurrent(x, S1, z);
        writePixel(canvas, FACE_TOP, x, z, c);
    }

    // ── Panel 1 — Back face  (z = 0) ───────────────────────────────────────
    //   lx = 63 - x, ly = 63 - y
    for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
        const uint32_t c = grid_.getCurrent(x, y, 0);
        writePixel(canvas, FACE_BACK, S1 - x, S1 - y, c);
    }

    // ── Panel 2 — Left face  (x = 0) ───────────────────────────────────────
    //   lx = z, ly = 63 - y
    for (int y = 0; y < S; ++y)
    for (int z = 0; z < S; ++z) {
        const uint32_t c = grid_.getCurrent(0, y, z);
        writePixel(canvas, FACE_LEFT, z, S1 - y, c);
    }

    // ── Panel 3 — Front face  (z = 63) ─────────────────────────────────────
    //   lx = x, ly = 63 - y
    for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
        const uint32_t c = grid_.getCurrent(x, y, S1);
        writePixel(canvas, FACE_FRONT, x, S1 - y, c);
    }

    // ── Panel 4 — Right face  (x = 63) ─────────────────────────────────────
    //   lx = 63 - z, ly = 63 - y
    for (int y = 0; y < S; ++y)
    for (int z = 0; z < S; ++z) {
        const uint32_t c = grid_.getCurrent(S1, y, z);
        writePixel(canvas, FACE_RIGHT, S1 - z, S1 - y, c);
    }

    // ── Panel 5 — Bottom face  (y = 0) ─────────────────────────────────────
    //   lx = x, ly = 63 - z
    for (int z = 0; z < S; ++z)
    for (int x = 0; x < S; ++x) {
        const uint32_t c = grid_.getCurrent(x, 0, z);
        writePixel(canvas, FACE_BOTTOM, x, S1 - z, c);
    }
}

// ── Tiny 4×5 bitmap font (A–Z) ────────────────────────────────────────────
//  Each row is 4 bits, MSB = leftmost pixel.
static const uint8_t FONT4x5[26][5] = {
    { 0x6,0x9,0xF,0x9,0x9 }, // A
    { 0xE,0x9,0xE,0x9,0xE }, // B
    { 0x7,0x8,0x8,0x8,0x7 }, // C
    { 0xE,0x9,0x9,0x9,0xE }, // D
    { 0xF,0x8,0xE,0x8,0xF }, // E
    { 0xF,0x8,0xE,0x8,0x8 }, // F
    { 0x7,0x8,0xB,0x9,0x7 }, // G
    { 0x9,0x9,0xF,0x9,0x9 }, // H
    { 0x6,0x4,0x4,0x4,0x6 }, // I
    { 0x1,0x1,0x1,0x9,0x6 }, // J
    { 0x9,0xA,0xC,0xA,0x9 }, // K
    { 0x8,0x8,0x8,0x8,0xF }, // L
    { 0x9,0xF,0xF,0x9,0x9 }, // M
    { 0x9,0xD,0xB,0x9,0x9 }, // N
    { 0x6,0x9,0x9,0x9,0x6 }, // O
    { 0xE,0x9,0xE,0x8,0x8 }, // P
    { 0x6,0x9,0xB,0x7,0x1 }, // Q
    { 0xE,0x9,0xE,0xA,0x9 }, // R
    { 0x7,0x8,0x6,0x1,0xE }, // S
    { 0xF,0x4,0x4,0x4,0x4 }, // T
    { 0x9,0x9,0x9,0x9,0x6 }, // U
    { 0x9,0x9,0xA,0x6,0x4 }, // V
    { 0x9,0x9,0xF,0xF,0x9 }, // W
    { 0x9,0xA,0x4,0xA,0x9 }, // X
    { 0x9,0xA,0x4,0x4,0x4 }, // Y
    { 0xF,0x1,0x6,0x8,0xF }, // Z
};

static void drawChar(rgb_matrix::FrameCanvas* canvas, int panelOx,
                     int ox, int oy, char ch,
                     uint8_t r, uint8_t g, uint8_t b) {
    if (ch < 'A' || ch > 'Z') return;
    const uint8_t* glyph = FONT4x5[ch - 'A'];
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 4; ++col)
            if (glyph[row] & (0x8u >> col))
                canvas->SetPixel(panelOx + ox + col, oy + row, r, g, b);
}

static void drawString(rgb_matrix::FrameCanvas* canvas, int panel,
                       const char* text, uint8_t r, uint8_t g, uint8_t b) {
    const int len     = static_cast<int>(std::strlen(text));
    const int totalW  = len * 4 + (len - 1);   // 4px char + 1px gap
    int ox            = (GRID_SIZE - totalW) / 2;
    const int oy      = (GRID_SIZE - 5) / 2;
    const int panelOx = panel * GRID_SIZE;
    for (int i = 0; i < len; ++i, ox += 5)
        drawChar(canvas, panelOx, ox, oy, text[i], r, g, b);
}

// ── Test pattern ───────────────────────────────────────────────────────────
//
//  Each panel gets a unique background colour and orientation markers:
//    • Top row  : white  — marks physical "up"  (after rotation is applied)
//    • Left col : white  — marks physical "left"
//    • (0,0) corner : yellow  — marks the top-left origin pixel
//
//  The rotation from panels.conf is applied here too, so the markers move
//  with the rotation.  When the white L sits in the physical top-left corner
//  the rotation value is correct.

void FaceMapper::renderTestPattern(rgb_matrix::FrameCanvas* canvas) const {
    canvas->Clear();

    static constexpr int S = GRID_SIZE;

    struct FaceInfo { const char* name; uint8_t r, g, b; };
    // Order matches physical chain: 0=Top, 1=Back, 2=Left, 3=Front, 4=Right, 5=Bottom
    static constexpr FaceInfo FACES[6] = {
        { "TOP",      0, 80, 80 },
        { "BACK",     0, 80,  0 },
        { "LEFT",    80, 80,  0 },
        { "FRONT",   80,  0,  0 },
        { "RIGHT",    0,  0, 80 },
        { "BOTTOM",  80,  0, 80 },
    };

    for (int p = 0; p < 6; ++p) {
        const auto& f = FACES[p];

        // Background fill (via writePixel so rotation is applied)
        for (int ly = 0; ly < S; ++ly)
        for (int lx = 0; lx < S; ++lx)
            writePixel(canvas, p, lx, ly,
                       (static_cast<uint32_t>(f.r) << 16) |
                       (static_cast<uint32_t>(f.g) <<  8) |
                        static_cast<uint32_t>(f.b));

        // Top row: white
        for (int lx = 0; lx < S; ++lx)
            writePixel(canvas, p, lx, 0, 0xFFFFFF);

        // Left column: white
        for (int ly = 0; ly < S; ++ly)
            writePixel(canvas, p, 0, ly, 0xFFFFFF);

        // Top-left corner: yellow
        writePixel(canvas, p, 0, 0, 0xFFFF00);

        // Face name — drawn directly (bypasses rotation intentionally so text
        // is always readable; adjust rotation until the L-marker is correct)
        drawString(canvas, p, f.name, 255, 255, 255);
    }
}
