# SandCube — Voxel Falling-Sand Animation for the LED Cube

A 64³ falling-sand cellular automaton for the [matrixserver](https://github.com/bjoernh/matrixserver) LED Cube. Grains pour in from the top face, pile up, and slide down diagonal slopes. Tilt the cube (or the smartphone running the CubeWebapp) and gravity follows — the heap flows toward the new "down".

The simulation is based on a double-buffered voxel grid driven by a simple cellular-automaton ruleset:

1. **Primary move** — try to fall one step along the current gravity direction.
2. **Diagonal slide** — if blocked, attempt the diagonals that still have a downward component. The eight candidates are tried in a per-particle randomised order (Wang-hashed) to avoid directional bias, sorted by how well they align with gravity.
3. **Rest** — if everything is blocked, stay in place.

All the panel-layout, chaining, and per-face rotation logic is handled by matrixserver itself. This app just pushes voxels with `setPixel3D` and the server takes care of mapping them onto the six physical 64×64 HUB75 panels.

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

When building SandCube, point CMake at the custom prefix:

```bash
cmake -DCMAKE_PREFIX_PATH=$HOME/.local ..
```

### Running the simulator

No physical cube required — matrixserver ships an all-in-one Docker simulator:

```bash
docker run -it --rm \
  -p 2017:2017 -p 1337:1337 -p 5173:5173 \
  ghcr.io/bjoernh/matrixserver-simulator:latest
```

Then open `https://localhost:5173` in a browser (accept the self-signed certificate) to see the 3D cube view.

---

## Build & run

```bash
git clone https://github.com/bjoernh/LEDCube-Voxel-Sand.git
cd LEDCube-Voxel-Sand

mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)

# Connect to a matrixserver running on localhost:2017
./SandCube

# Or point it at a remote / different server
./SandCube "tcp://192.168.1.100:2017"
```

---

## Input sources

SandCube reads the gravity vector from one of two `OrientationSource` implementations. The active source can be switched at runtime from the CubeWebapp parameter panel (**Control → Gravity source**).

### IMU (default) — `ImuOrientation`

A thin wrapper around matrixserver's `Mpu6050` helper. Works with:

- A real **MPU-6050 / GY-521** connected via I²C on the Raspberry Pi. Make sure I²C is enabled (`raspi-config → Interfaces → I2C`) and the user running SandCube is in the `i2c` group.
- A **smartphone** opened on the CubeWebapp — the browser's DeviceMotion API forwards orientation data to matrixserver, which surfaces it through the same `Mpu6050` static API. Tilt your phone and the sand follows. This also makes the simulator fully usable without any hardware sensor.

The raw acceleration vector is normalised and handed straight to the sand engine; `SandEngine::rebuildSlideDirs()` turns it into a discrete primary move direction plus a sorted set of diagonal slide candidates.

### Keyboard — `KeyboardTilt`

Fallback source that reads WASD on stdin (raw terminal mode, background thread):

| Key | Effect |
|---|---|
| `w` / `s` | Tilt gravity along +Z / −Z |
| `a` / `d` | Tilt gravity along −X / +X |
| `r` | Reset to straight-down `(0, -1, 0)` |

Selecting the **Keyboard** source in the CubeWebapp lazily constructs the `KeyboardTilt` object the first time it is needed.

---

## Live parameters

Exposed through matrixserver's `AnimationParams` system — tweak them live from the CubeWebapp UI, or save/load sets as JSON presets:

| Name | Group | Type | Range | Default | Purpose |
|---|---|---|---|---|---|
| `spawnEveryN` | Sand | int | 1 – 30 | 2 | Frames between spawn bursts |
| `burst` | Sand | int | 0 – 200 | 25 | Grains per burst |
| `fade` | Sand | float | 0.0 – 1.0 | 0.0 | Motion-trail fade amount (0 = crisp) |
| `orientation` | Control | enum | IMU \| Keyboard | IMU | Active gravity source |

Brightness is intentionally **not** exposed here — matrixserver has its own global brightness control in the CubeWebapp.

---

## Project layout

```
LEDCube-Voxel-Sand/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── Hash.h             — Wang-hash helper (bias-free int randomness)
│   ├── VoxelGrid.h        — 64³ double-buffered voxel store
│   ├── SandEngine.h       — cellular-automaton physics + Gravity struct
│   ├── SandSpawner.h      — gravity-aware grain source
│   ├── OrientationSource.h — abstract gravity provider interface
│   ├── KeyboardTilt.h     — stdin WASD source
│   └── ImuOrientation.h   — matrixserver Mpu6050 source
└── src/
    ├── main.cpp           — entry point
    ├── SandCube.{h,cpp}   — CubeApplication subclass (the glue)
    ├── VoxelGrid.cpp
    ├── SandEngine.cpp
    ├── SandSpawner.cpp
    ├── KeyboardTilt.cpp
    └── ImuOrientation.cpp
```

---

## How it works

**`VoxelGrid`** — A flat `std::vector<uint32_t>` of size 64³ with colour encoded as `0x00RRGGBB` (zero means empty). Physics is double-buffered: each tick reads from the *current* buffer and writes to the *next*, then the two are swapped in O(1). This keeps the simulation deterministic and avoids "chain reactions" where a grain moves twice in one tick.

**`SandEngine`** — Owns the grid and rebuilds a slide-direction table whenever gravity changes. For each occupied voxel it tries the primary gravity step, then each diagonal that still has a positive dot product with the gravity vector, and finally stays in place. The starting index into the diagonal list is Wang-hashed from the voxel's address XOR'd with a frame counter, eliminating systematic spatial bias without any per-cell allocations.

**`SandSpawner`** — Injects grains from the face whose outward normal is most opposite the current gravity vector. The two free axes on that face are filled with independent Wang-hash values so grains are spread across the full face instead of stacking in one column. Tilt the cube and grains always enter from the new "top".

**`SandCube`** (the matrixserver app) — Registers the live `AnimationParams`, reads the chosen `OrientationSource` each frame, feeds gravity into the engine, runs one physics tick, and renders the six outer face planes via `setPixel3D`. Interior voxels are never touched because they can't be seen — that keeps the draw loop at 6 × 64² ≈ 24k pixels per frame instead of the full 262k volume.
