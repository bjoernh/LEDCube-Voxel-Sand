#pragma once

#include "VoxelGrid.h"
#include <led-matrix.h>   // hzeller/rpi-rgb-led-matrix
#include <array>

// ──────────────────────────────────────────────────────────────────────────
//  FaceMapper
//
//  Maps the outer shell of the 64³ VoxelGrid onto 6 × HUB75 LED panels
//  (each 64 × 64 pixels) connected as  --led-chain=6 --led-parallel=1.
//
//  Panel layout in FrameCanvas  (canvas: 384 × 64)
//  ─────────────────────────────────────────────────
//   Panel index │ Face      │ Plane equation │ Canvas region
//   ────────────┼───────────┼────────────────┼────────────────────────────
//       0       │ Top       │ y = 63         │ x=[0..63],    y=[0..63]
//       1       │ Back      │ z =  0         │ x=[64..127],  y=[0..63]
//       2       │ Left      │ x =  0         │ x=[128..191], y=[0..63]
//       3       │ Front     │ z = 63         │ x=[192..255], y=[0..63]
//       4       │ Right     │ x = 63         │ x=[256..319], y=[0..63]
//       5       │ Bottom    │ y =  0         │ x=[320..383], y=[0..63]
//
//  Coordinate conventions  (physical "up" = +y for vertical faces)
//  ─────────────────────────────────────────────────────────────────
//  Panel (lx,ly) with ly=0 at the top of the physical panel:
//
//   Front  (z=63) : lx = x,      ly = 63-y
//   Back   (z= 0) : lx = 63-x,   ly = 63-y   ← mirrored to preserve handedness
//   Right  (x=63) : lx = 63-z,   ly = 63-y   ← z=63 is left when viewed from outside
//   Left   (x= 0) : lx = z,      ly = 63-y
//   Top    (y=63) : lx = x,      ly = z       ← +z = away from viewer looking down
//   Bottom (y= 0) : lx = x,      ly = 63-z    ← +z = toward viewer looking up
//
//  Edge-continuity check (adjacent panels share a boundary pixel column/row
//  at the same (lx or ly) value with offset 0/63):
//   Front↔Right : front(lx=63, r) ↔ right(lx=0,  r)   ✓
//   Front↔Left  : front(lx=0,  r) ↔ left (lx=63, r)   ✓
//   Front↔Top   : front(r, ly=0)  ↔ top  (r, ly=63)    ✓
//   Front↔Bottom: front(r, ly=63) ↔ bottom(r, ly=63)   ✓
//   Back↔Right  : back (lx=0,  r) ↔ right(lx=63, r)   ✓
//   Back↔Left   : back (lx=63, r) ↔ left (lx=0,  r)   ✓
// ──────────────────────────────────────────────────────────────────────────

class FaceMapper {
public:
    /// Loads rotation config from panels.conf (falls back to 0° if missing).
    explicit FaceMapper(const VoxelGrid& grid,
                        const char* configPath = "panels.conf");

    /// Write all 6 outer faces to canvas.
    /// Pixels with voxel value 0 are written as black (off).
    void renderToPanels(rgb_matrix::FrameCanvas* canvas) const;

    /// Show a static orientation test pattern on every panel.
    /// Each panel gets a unique colour plus white top/left edges and a yellow
    /// top-left corner so you can verify face assignment and rotation.
    /// Run with: sudo ./voxel-sand --test
    void renderTestPattern(rgb_matrix::FrameCanvas* canvas) const;

private:
    /// Decode 0x00RRGGBB → r, g, b components.
    static void unpackColor(uint32_t color,
                            uint8_t& r, uint8_t& g, uint8_t& b) noexcept;

    /// Apply panel rotation then write to the correct canvas region.
    void writePixel(rgb_matrix::FrameCanvas* canvas,
                    int panel, int lx, int ly, uint32_t color) const;

    /// Load per-panel rotations from config file.
    void loadConfig(const char* path);

    const VoxelGrid& grid_;
    std::array<int, 6> rotation_{};   // degrees CW per panel, default 0
};
