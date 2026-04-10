# 3D LED Sand Cube — Phase 1 Engine

A 64³ falling-sand cellular automaton that renders its outer surface to
six 64×64 HUB75 LED panels driven by a Raspberry Pi 4.

---

## Project layout

```
sand_cube/
├── CMakeLists.txt
├── include/
│   ├── VoxelGrid.h    — 64³ double-buffered voxel store
│   ├── SandEngine.h   — cellular automata physics + Gravity struct
│   └── FaceMapper.h   — 3-D surface → 6-panel 2-D mapping
└── src/
    ├── VoxelGrid.cpp
    ├── SandEngine.cpp
    ├── FaceMapper.cpp
    └── main.cpp       — matrix init, 60 FPS loop, sand spawner
```

---

## Dependencies

| Component | Source |
|---|---|
| rpi-rgb-led-matrix | https://github.com/hzeller/rpi-rgb-led-matrix |
| C++17 compiler | `sudo apt install build-essential cmake` |

---

## Building

```bash
# 1. Clone the LED library next to this project
cd ~
git clone https://github.com/hzeller/rpi-rgb-led-matrix.git
cd rpi-rgb-led-matrix && make -C lib   # build librgbmatrix.a

# 2. Build SandCube
cd ~/sand_cube
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

# 3. Run (requires root for GPIO access)
sudo ./build/sand_cube
```

If the library is in a non-standard location:
```bash
cmake -B build -DRGBMATRIX_DIR=/path/to/rpi-rgb-led-matrix
```

---

## Hardware wiring

```
Raspberry Pi 4
    HUB75 connector A  (parallel 0) — chain of 2 panels  → Front (panel 0)
                                                          → Back  (panel 1)
    HUB75 connector B  (parallel 1) — chain of 2 panels  → Right (panel 2)
                                                          → Left  (panel 3)
    HUB75 connector C  (parallel 2) — chain of 2 panels  → Top   (panel 4)
                                                          → Bottom(panel 5)
```

Command-line flags (embedded in `createMatrix()` in `main.cpp`):
```
--led-rows=64 --led-cols=64 --led-chain=2 --led-parallel=3
--led-brightness=50 --led-gpio-slowdown=4
```

Adjust `gpio_slowdown` if you see pixel glitches:  4 for RPi 4, 2–3 for RPi 3.

---

## Physical panel mounting

Each **vertical** face panel (Front / Back / Right / Left) must be mounted
with **panel row 0 facing the physical top of the cube** (+y direction).

The **Top** panel should be mounted with:
  - `lx` increasing toward the right side of the cube (+x)
  - `ly` increasing toward the Back face (+z)

The **Bottom** panel mirrors the Top: `ly` increases toward the Front face (-z).

### Edge continuity at seams

| Edge | Face A (col/row) | Face B (col/row) |
|---|---|---|
| Front ↔ Right  | Front  col 63 | Right  col 0  |
| Front ↔ Left   | Front  col 0  | Left   col 63 |
| Front ↔ Top    | Front  row 0  | Top    row 63 |
| Front ↔ Bottom | Front  row 63 | Bottom row 63 |
| Back  ↔ Right  | Back   col 0  | Right  col 63 |
| Back  ↔ Left   | Back   col 63 | Left   col 0  |

---

## Key design decisions

### Double buffering & cascade behaviour
`SandEngine::update()` reads from `current_` to decide whether a cell is
occupied, but checks `next_` for conflict resolution when placing a particle.
A cell vacated earlier in the same tick appears empty to later particles,
enabling cascade/flow moves (sand can fall more than one cell per tick).

To disable cascades and enforce strict "one cell per tick" motion, set
`STRICT_ONE_STEP = true` at the top of `SandEngine.cpp`.

### Anti-directional-bias
The slide-direction array is rotated by a per-particle starting index derived
from a Wang hash of `(grid_address XOR frame_counter)`.  This is O(1) and
allocation-free in the hot loop while eliminating any systematic spatial bias
in how sand slides around obstacles.

### Gravity
```cpp
// Change gravity at runtime:
engine.setGravity({0, -1,  0});   // down    (default)
engine.setGravity({0,  1,  0});   // up
engine.setGravity({1,  0,  0});   // +x
engine.setGravity({-1, 0,  0});   // -x
engine.setGravity({0,  0,  1});   // +z
engine.setGravity({0,  0, -1});   // -z
```
Diagonal gravity (e.g. `{1, -1, 0}`) is also supported; the slide-direction
table is recomputed automatically.

---

## Performance notes

| Stage | Cost (RPi 4, Release) |
|---|---|
| `engine.update()` — 262 144 voxels | ~1–2 ms |
| `mapper.renderToPanels()` — 6×64×64 pixels | ~0.5 ms |
| `SwapOnVSync()` — hardware DMA | locked to VSYNC |

Total headroom at 60 FPS (~16.7 ms budget): ≈ 14 ms available for future
effects, IMU gravity input, or network sync.
