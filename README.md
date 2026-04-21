# LEDCube-Voxel-Sand

Two real-time physics simulations for the [matrixserver](https://github.com/bjoernh/matrixserver) 64×64×64 LED Cube:

**SandCube** — A 3D falling-sand cellular automaton. Grains pour in from the top face, pile up, and slide down diagonal slopes. Tilt the cube (or the smartphone running the CubeWebapp) and gravity follows — the heap flows toward the new "down".

**WaterCube** — A PIC/FLIP fluid simulation. ~2600 particles live on a coarse 20³ MAC grid and rasterise onto the 64³ outer shell for rendering. Water sloshes and settles smoothly as the cube is tilted.

Both simulations share the same IMU/keyboard gravity input and render onto the six physical 64×64 HUB75 panels via `setPixel3D`.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Setting up matrixserver](#setting-up-matrixserver)
3. [Build & run](#build--run)
4. [Input sources](#input-sources)
5. [Live parameters](#live-parameters)
6. [Project layout](#project-layout)
7. [How it works](#how-it-works)

---

## Prerequisites

| Dependency | Minimum Version | Notes |
|---|---|---|
| C++ Compiler | **C++20** | GCC 10+ or Clang 10+ |
| CMake | 3.7 | Build system |
| `libmatrixapplication` | 0.4 | From the matrixserver project |
| Boost | 1.58.0 | Components: `thread`, `log` |
| Protobuf | Any | `libprotobuf-dev` + `protobuf-compiler` |
| Eigen3 | Any | Header-only linear algebra (`libeigen3-dev`) |
| pkg-config | Any | Used by CMake to locate Eigen3 |

**Debian / Ubuntu / Raspberry Pi OS:**

```bash
sudo apt install \
  cmake g++ \
  libboost-all-dev \
  libprotobuf-dev protobuf-compiler \
  libeigen3-dev \
  pkg-config
```

---

## Setting up matrixserver

`libmatrixapplication` ships with the [matrixserver](https://github.com/bjoernh/matrixserver) project. You have two options:

**Option A — pre-built Debian package (recommended):**
Grab the matching `.deb` from the [matrixserver Releases page](https://github.com/bjoernh/matrixserver/releases) and install it:

```bash
sudo dpkg -i matrixserver-*.deb
```

**Option B — build from source:**

```bash
git clone --recursive https://github.com/bjoernh/matrixserver.git
cd matrixserver
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local ..
make -j$(nproc)
make install
```

When building, point CMake at the custom prefix:

```bash
cmake -DCMAKE_PREFIX_PATH=$HOME/.local -B build
```

### Running the simulator

No physical cube required — matrixserver ships an all-in-one Docker simulator:

```bash
docker run -it --rm \
  -p 2017:2017 -p 1337:1337 -p 5173:5173 \
  ghcr.io/bjoernh/matrixserver-simulator:latest
```

Open `https://localhost:5173` in a browser (accept the self-signed certificate) to see the 3D cube view and the parameter panel.

---

## Build & run

```bash
git clone https://github.com/bjoernh/LEDCube-Voxel-Sand.git
cd LEDCube-Voxel-Sand

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### SandCube

```bash
./build/SandCube                        # connect to localhost:2017
./build/SandCube "tcp://192.168.1.x:2017"  # remote server
./build/SandCube --imu-debug            # show gravity vector as a red dot
```

### WaterCube

```bash
./build/WaterCube                        # connect to localhost:2017
./build/WaterCube "tcp://192.168.1.x:2017"
```

---

## Input sources

Both apps read the gravity vector from one of two `OrientationSource`
implementations. The active source is switched at runtime from the CubeWebapp
parameter panel (**Control → Gravity source**).

### IMU (default) — `ImuOrientation`

A thin wrapper around matrixserver's `Mpu6050` helper. Works with:

- A real **MPU-6050 / GY-521** connected via I²C on the Raspberry Pi. Make
  sure I²C is enabled (`raspi-config → Interfaces → I2C`) and the user
  running the app is in the `i2c` group.
- A **smartphone** opened on the CubeWebapp — the browser's DeviceMotion API
  forwards orientation data to matrixserver, which surfaces it through the
  same `Mpu6050` API. Tilt your phone and gravity follows. This makes the
  Docker simulator fully interactive without any hardware sensor.

The raw acceleration vector is normalised to a unit `Gravity{x, y, z}` struct
and handed to the engine. The z-axis is vertical (`z+ = downward`).

### Keyboard — `KeyboardTilt`

Fallback source that reads WASD on stdin (raw terminal mode, background thread):

| Key | Effect |
|---|---|
| `w` / `s` | Tilt gravity along +Z / −Z |
| `a` / `d` | Tilt gravity along −X / +X |
| `r` | Reset to straight-down `{0, 0, 1}` |

---

## Live parameters

Exposed through matrixserver's `AnimationParams` system — tweak them live
from the CubeWebapp UI.

### SandCube

| Name | Group | Type | Range | Default | Purpose |
|---|---|---|---|---|---|
| `spawnEveryN` | Sand | int | 1 – 30 | 2 | Frames between spawn bursts |
| `burst` | Sand | int | 0 – 200 | 25 | Grains per burst |
| `fade` | Sand | float | 0.0 – 1.0 | 0.0 | Motion-trail fade (0 = crisp) |
| `orientation` | Control | enum | IMU \| Keyboard | IMU | Active gravity source |

### WaterCube

| Name | Group | Type | Range | Default | Purpose |
|---|---|---|---|---|---|
| `gravityMagnitude` | Physics | float | 0 – 60 | 25 | Gravity strength (cells/s²) |
| `flipBlend` | Physics | float | 0.0 – 1.0 | 0.95 | FLIP blend (0 = pure PIC, 1 = pure FLIP) |
| `jacobiIterations` | Physics | int | 5 – 80 | 30 | Pressure solver iterations |
| `fillLevel` | Fluid | float | 0.0 – 1.0 | 0.40 | Fraction of cube volume filled |
| `refill` | Fluid | enum | idle \| refill | idle | Trigger to re-pour the fluid |
| `orientation` | Control | enum | IMU \| Keyboard | IMU | Active gravity source |

---

## Project layout

```
LEDCube-Voxel-Sand/
├── CMakeLists.txt
├── README.md
├── CLAUDE.md               — instructions for Claude Code
├── include/
│   ├── Gravity.h           — shared {x,y,z} gravity unit vector
│   ├── OrientationSource.h — abstract gravity provider interface
│   ├── ImuOrientation.h    — matrixserver Mpu6050 source
│   ├── KeyboardTilt.h      — stdin WASD source
│   ├── Hash.h              — Wang-hash helper (bias-free randomness)
│   ├── VoxelGrid.h         — 64³ double-buffered voxel store
│   ├── SandEngine.h        — cellular-automaton physics
│   ├── SandSpawner.h       — gravity-aware grain spawner
│   └── FluidEngine.h       — PIC/FLIP fluid physics
└── src/
    ├── main.cpp            — SandCube entry point (--imu-debug flag)
    ├── SandCube.{h,cpp}    — sand CubeApplication subclass
    ├── VoxelGrid.cpp
    ├── SandEngine.cpp
    ├── SandSpawner.cpp
    ├── ImuOrientation.cpp
    ├── KeyboardTilt.cpp
    ├── water_main.cpp      — WaterCube entry point
    ├── WaterCube.{h,cpp}   — fluid CubeApplication subclass
    └── FluidEngine.cpp
```

---

## How it works

### SandCube

**`VoxelGrid`** — A flat `std::vector<uint32_t>` of size 64³ with colour
encoded as `0x00RRGGBB` (zero = empty). Physics is double-buffered: each tick
reads from *current* and writes to *next*, then the two swap in O(1). Each
grain advances at most once per tick, preventing cascade chain reactions.

**`SandEngine`** — Rebuilds a slide-direction table whenever gravity changes.
For each occupied voxel it tries (A) the primary gravity step, (B) each
diagonal with a positive dot product against gravity, then (C) stays in place.
The starting index into the diagonal list is Wang-hashed from the voxel
address XOR'd with a frame counter, eliminating systematic spatial bias without
per-cell allocations.

**`SandSpawner`** — Injects grains from the face whose outward normal is most
opposite the current gravity vector. Two free axes are Wang-hash spread so
grains cover the full face. Tilt the cube and grains always enter from the new
"top".

**`SandCube`** — Registers live `AnimationParams`, reads the active
`OrientationSource` each frame, feeds gravity to the engine, runs one physics
tick, then renders the six outer face planes via `setPixel3D`. Interior voxels
are never touched — only the ~24k surface pixels per frame need drawing.

### WaterCube

**`FluidEngine`** — Implements PIC/FLIP on a staggered 20³ MAC grid. Each
frame: classify cells (SOLID/FLUID/AIR) from particle positions → scatter
particle velocities to grid faces (P2G) → apply gravity body force → enforce
solid-wall boundary conditions → solve pressure (Jacobi) → project velocities
→ blend grid delta back onto particles (FLIP/PIC) → advect particles with
Euler integration + wall clamping.

**Rendering** — `FluidEngine::renderSurface` walks the 6 outer face planes of
the 64³ render grid, looks up the corresponding sim cell, and shades FLUID
cells with a blue gradient based on depth along the gravity axis (shallow =
bright cyan, deep = dark navy). Surface cells (adjacent to AIR) get an
additive brightness highlight.

**`WaterCube`** — Registers live `AnimationParams` (gravity magnitude, FLIP
blend, Jacobi iterations, fill level, refill trigger), reads orientation,
drives `FluidEngine`, and renders via `setPixel3D`. Grid coordinates [0–63]
are mapped to the virtual cube coordinate space [0–65] that `setPixel3D`
requires so all six physical screens render correctly.
