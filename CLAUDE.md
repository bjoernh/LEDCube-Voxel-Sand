# LEDCube-Voxel-Sand — Claude Code Instructions

## Project

Two real-time physics simulations for a 64×64×64 LED cube, both built on the
matrixserver `libmatrixapplication` framework:

- **SandCube** — 3D falling-sand cellular automaton. Grains pour from the
  "top" face, pile up, and slide along diagonals. Gravity follows the IMU (or
  phone tilt via the CubeWebapp).
- **WaterCube** — PIC/FLIP fluid simulation on a 20³ MAC grid. ~2600
  particles carry position/velocity; they rasterise onto the 64³ outer shell
  for rendering. Water settles under gravity and sloshes when the cube is
  tilted.

Six 64×64 HUB75 panels driven via matrixserver show the outer shell of the
voxel grid in real time at 60 FPS.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

./build/SandCube                       # sand sim, connect to localhost
./build/SandCube "tcp://host:2017"     # sand sim, remote server
./build/SandCube --imu-debug           # sand sim + red gravity-vector dot

./build/WaterCube                      # fluid sim, connect to localhost
./build/WaterCube "tcp://host:2017"    # fluid sim, remote server
```

Debug build: `-DCMAKE_BUILD_TYPE=Debug` (adds `-Og -g3`).  
All builds enforce `-Wall -Wextra -Wpedantic` — zero warnings is the standard.

## Architecture

```
Gravity.h          — shared {x,y,z} unit vector (z+ = down by convention)
OrientationSource  — abstract interface; two implementations:
  ImuOrientation   — reads matrixserver Mpu6050 (I²C hardware or phone DeviceMotion)
  KeyboardTilt     — WASD on stdin (raw terminal, background thread)

SandCube app:
  VoxelGrid        — 64³ double-buffered flat array (0x00RRGGBB voxels)
  SandEngine       — cellular automata physics (owns VoxelGrid)
  SandSpawner      — pours grains into the engine each frame
  SandCube         — CubeApplication subclass; glues engine → setPixel3D

WaterCube app:
  FluidEngine      — PIC/FLIP on a 20³ staggered MAC grid, ~2600 particles
  WaterCube        — CubeApplication subclass; glues engine → setPixel3D
```

Data flows one way in both apps:
`Engine → renderSurface callback → setPixel3D → matrixserver → panels`

## Coordinate system (critical)

`setPixel3D(x, y, z)` in the matrixserver `CubeApplication` uses a **virtual
cube** coordinate space of size 66³ (indices 0–65, `VIRTUALCUBEMAXINDEX = 65`).
Only pixels whose coordinate hits a boundary value (0 or 65) are routed to a
physical screen:

| Boundary | Screen | Physical face |
|---|---|---|
| y == 0  | screen[0] | front  |
| x == 65 | screen[1] | right  |
| y == 65 | screen[2] | back   |
| x == 0  | screen[3] | left   |
| z == 0  | screen[4] | top    |
| z == 65 | screen[5] | bottom |

**z+ is downward.** When the cube is upright the IMU returns a +z acceleration
component (~1 g) and both engines use `Gravity{0, 0, 1}` as their default.

### Grid-to-virtual coordinate mapping

Both engines produce render coordinates in the grid range [0, 63]
(`CUBEMAXINDEX = 63`). These must be mapped before calling `setPixel3D`:

- Face axis at **0** → virtual **0** (boundary, triggers screen routing)
- Face axis at **63** → virtual **65** (boundary, triggers screen routing)
- Free axes [0..63] → virtual [1..64] (lands on physical panel pixels [0..63])

**If you forget this mapping, grid face 63 ≠ virtual 65, and the back, right,
and bottom screens will be completely blank.** The SandCube fix is in
`SandCube::renderSurface`; the WaterCube fix is the `toV()` lambda in
`WaterCube::loop`.

## Key design decisions

### SandCube

**Double buffering**: physics reads `current_`, writes `next_`, then swaps
O(1). Each grain advances at most once per tick — required for correct
pile-building.

**Anti-bias randomisation**: Wang hash (`include/Hash.h`) of
`(voxel_address XOR frame_counter)` picks the starting index in the
per-gravity slide-direction table. No allocation, no systematic directional
bias.

**Slide directions**: for gravity `g`, all 26-neighbour vectors `v` with
`dot(v,g) > 0` and `v ≠ g` qualify. Sorted by `dot(v,g)` descending.
Recomputed on every `setGravity()` call.

**Spawner**: grains enter from the face whose outward normal is most
opposite the current gravity vector. Two free axes are Wang-hash spread so
grains cover the whole face.

### WaterCube

**PIC/FLIP blend**: `flipBlend_` (default 0.95) controls how much of the
FLIP velocity delta (energetic, long-range) vs. pure PIC (damped) is used
when transferring grid velocities back to particles.

**Pressure solve**: Jacobi iterations on the staggered MAC grid. Dirichlet
zero pressure at AIR neighbours, Neumann at SOLID walls.

**renderSurface shading**: depth along the gravity axis from the fluid
centroid determines the blue gradient (shallow = bright, deep = dark).
Surface cells (adjacent to AIR) get an additive highlight.

## IMU / Mpu6050 notes

The matrixserver `Mpu6050` class applies a 45° rotation in the (IMU_X, IMU_Z)
plane to compensate for the physical sensor mounting angle. The final
acceleration vector is:

```
acceleration[0] = imu_ax * cos45 - imu_az * sin45   → cube X (left/right)
acceleration[1] = imu_ax * sin45 + imu_az * cos45   → cube Y (front/back)
acceleration[2] = imu_ay                             → cube Z (up/down, vertical)
```

**Do not call `Mpu6050::init()` twice.** The constructor already calls it and
starts the I²C refresh thread. A second `init()` starts a second thread that
races on the same file descriptor and garbles axis readings. `ImuOrientation`
was previously affected by this bug; it now leaves construction entirely to
the `Mpu6050` constructor.

## File map

| Path | Role |
|---|---|
| `include/Gravity.h` | Shared `Gravity{x,y,z}` struct; default `{0,0,1}` = z-down |
| `include/OrientationSource.h` | Abstract `getGravity()` interface |
| `include/ImuOrientation.h` | Mpu6050-backed orientation source |
| `include/KeyboardTilt.h` | WASD stdin source |
| `include/Hash.h` | Shared `wangHash()` function |
| `include/VoxelGrid.h` | Grid interface + `GRID_SIZE` / `GRID_TOTAL` constants |
| `include/SandEngine.h` | Sand physics interface |
| `include/SandSpawner.h` | Spawner interface + default palette |
| `include/FluidEngine.h` | Fluid physics interface; `SIM_N=20`, `PixelCb` typedef |
| `src/ImuOrientation.cpp` | Normalises acceleration → `Gravity`; no double-init |
| `src/KeyboardTilt.cpp` | Raw-terminal WASD reader |
| `src/VoxelGrid.cpp` | Double-buffer swap, bounds check, spawn |
| `src/SandEngine.cpp` | Rules A/B/C, slide table, Wang-hash anti-bias |
| `src/SandSpawner.cpp` | Burst spawning with Wang-hash spread |
| `src/SandCube.{h,cpp}` | Sand app: params, loop, `renderSurface` (with coord mapping) |
| `src/main.cpp` | Sand entry point; parses `--imu-debug` flag |
| `src/FluidEngine.cpp` | Full PIC/FLIP: P2G, gravity, pressure, G2P, advect, render |
| `src/WaterCube.{h,cpp}` | Fluid app: params, loop, `toV()` coord mapping |
| `src/water_main.cpp` | Fluid entry point |

## Constraints

- No backward compatibility needed — this is early-stage code.
- Target hardware: Raspberry Pi 4, ARMv8. `-march=native` is intentional.
- Physics hot loops (`SandEngine::update`, `FluidEngine::update`) must stay
  allocation-free.
- `libmatrixapplication` is resolved by CMake `find_package`; IDE LSP may
  show spurious errors for its headers — ignore them.
