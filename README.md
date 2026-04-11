# LEDCube App Template

A ready-to-use CMake project template for writing LED Cube applications with the **libmatrixapplication** framework from the [matrixserver](https://github.com/bjoernh/matrixserver) project.

> **How it works:** Your application is a C++ program that connects to a running `matrixserver` instance. The server acts as a display driver — it receives pixel data from your app and forwards it to the physical LED panels (or the software simulator). Your app only needs to implement a single `loop()` function that is called at a configurable frame rate.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Setting up matrixserver](#setting-up-matrixserver)
3. [Getting the Template](#getting-the-template)
4. [Build Instructions](#build-instructions)
5. [Running Your Application](#running-your-application)
6. [Template Structure](#template-structure)
7. [Writing Your Application](#writing-your-application)
   - [CubeApplication API Reference](#cubeapplication-api-reference)
   - [Dynamic Parameters (AnimationParams)](#dynamic-parameters-animationparams)
8. [Joystick Input](#joystick-input)
9. [IMU Input (MPU-6050)](#imu-input-mpu-6050)
10. [Audio Input](#audio-input)
11. [Renaming the Template](#renaming-the-template)

---

## Prerequisites

The following must be installed on your development system before you can build an app with this template:

| Dependency | Minimum Version | Notes |
|---|---|---|
| C++ Compiler | C++17 | GCC 7+, Clang 5+, or MSVC 2017+(untested) |
| CMake | 3.7 | Build system |
| `libmatrixapplication` | 0.4 | From the `matrixserver` project (see below) |
| Boost | 1.58.0 | Components: `thread`, `log` |
| Protobuf | Any | `libprotobuf-dev` + `protobuf-compiler` |
| Eigen3 | Any | Header-only linear algebra library (`libeigen3-dev`) |
| Imlib2 | Any | Image loading (`libimlib2-dev`) |
| pkg-config | Any | Used by CMake to locate Eigen3 and Imlib2 |

**On Ubuntu / Raspbian / Debian**, install all dependencies in one command:

```bash
sudo apt install \
  cmake g++ \
  libboost-all-dev \
  libprotobuf-dev protobuf-compiler \
  libeigen3-dev \
  libimlib2-dev \
  pkg-config
```

---

## Setting up matrixserver

`libmatrixapplication` is part of the [matrixserver](https://github.com/bjoernh/matrixserver) project. You must have this library installed on your system (or available via `CMAKE_PREFIX_PATH`) before building any application with this template.

You have two options:

### Option A — Install a pre-built `.deb` package (recommended)

Pre-compiled Debian packages for `amd64` (desktop/simulator) and `arm64` (Raspberry Pi) are available on the [matrixserver Releases page](https://github.com/bjoernh/matrixserver/releases).

```bash
# Download the .deb package for your platform from the releases page, then:
sudo dpkg -i matrixserver-*.deb
```

This installs `libmatrixapplication` and its headers system-wide — CMake will find it automatically.

### Option B — Build and install from source

```bash
# Clone the matrixserver repository (with submodules)
git clone --recursive https://github.com/bjoernh/matrixserver.git
cd matrixserver

# Build and install to a local prefix
mkdir -p build && cd build
cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local ..
make -j$(nproc)
make install
```

When building your app you will then need to tell CMake where to find it:

```bash
cmake -DCMAKE_PREFIX_PATH=$HOME/.local ..
```

### Running the Simulator

The matrixserver project ships a ready-to-use **Docker-based simulator** so you can test your application without physical LED hardware. Pull and start it with:

```bash
docker run -it --rm \
  -p 2017:2017 \
  -p 1337:1337 \
  -p 5173:5173 \
  ghcr.io/bjoernh/matrixserver-simulator:latest
```

Then open your browser at `https://localhost:5173` (accept the self-signed certificate warning) and connect the CubeWebapp to `wss://localhost:5173/matrix-ws`. Once your application is running, you will see it rendered in the 3D simulator.

*See the [matrixserver README](https://github.com/bjoernh/matrixserver#quick-start-all-in-one-simulator-container) for full Docker Compose instructions and port details.*

---

## Getting the Template

```bash
git clone https://github.com/<your-username>/LEDCube-App-Template.git
cd LEDCube-App-Template
```

---

## Build Instructions

```bash
# 1. Create a build directory
mkdir build && cd build

# 2. Configure with CMake
cmake ..

# If libmatrixapplication is installed to a custom prefix, add:
#   cmake -DCMAKE_PREFIX_PATH=/path/to/install ..

# 3. Compile
cmake --build .
```

The resulting binary is `build/AppTemplate`.

---

## Running Your Application

Make sure `matrixserver` (or the Docker simulator) is already running, then start your app:

```bash
./AppTemplate
```

By default the app connects to `tcp://127.0.0.1:2017`. To connect to a different server address pass it as the first argument:

```bash
./AppTemplate "tcp://192.168.1.100:2017"
```

---

## Template Structure

```
LEDCube-App-Template/
├── CMakeLists.txt        ← Build configuration
└── src/
    ├── main.cpp          ← Entry point, parses server URI, starts the app
    ├── AppTemplate.h     ← Application class declaration
    └── AppTemplate.cpp   ← Application logic — implement your loop() here
```

### `src/main.cpp`

The entry point is minimal by design. It reads an optional server URI from the command line, instantiates your application class, and calls `start()`. The main thread then sleeps — all frame logic runs in the background thread managed by the framework.

### `src/AppTemplate.h` / `src/AppTemplate.cpp`

Your application class inherits from `CubeApplication` and only needs to override one method:

```cpp
bool loop();
```

`loop()` is called repeatedly at the configured frame rate. Return `true` to continue, `false` to stop.

---

## Writing Your Application

### CubeApplication API Reference

Your class inherits from `CubeApplication`, which itself extends `MatrixApplication`. The cube has 6 faces, each 64×64 pixels.

#### Constructor

```cpp
CubeApplication(int fps, std::string serverUri, std::string appName);
```

| Parameter | Default | Description |
|---|---|---|
| `fps` | `40` | Target frame rate (1–200 fps) |
| `serverUri` | `"tcp://127.0.0.1:2017"` | Address of the matrixserver |
| `appName` | `"CubeApp"` | Name shown in the CubeWebapp UI |

#### Screen Identifiers

The six faces of the cube are referenced by the `ScreenNumber` enum:

| Value | Face |
|---|---|
| `front` | Front face |
| `right` | Right face |
| `back` | Back face |
| `left` | Left face |
| `top` | Top face |
| `bottom` | Bottom face |
| `anyScreen` | Wildcard (used by some helper queries) |

#### Frame Lifecycle Methods

| Method | Description |
|---|---|
| `clear()` | Fill all pixels with black |
| `render()` | Flush the current frame to the matrixserver |
| `fillAll(Color col)` | Fill all faces with a single colour |
| `fade(float factor)` | Multiply all pixel values by `factor` (0.0–1.0) for a fade/trail effect |

Always call `clear()` (or `fade()`) at the start of each frame and `render()` at the end.

#### 3D Drawing Methods

Coordinates range from **0 to 63** on each axis (the cube face is 64×64).

```cpp
// Set a single voxel
void setPixel3D(int x, int y, int z, Color col, float intensity = 1.0f, bool add = false);
void setPixel3D(Vector3i pos, Color col, float intensity = 1.0f, bool add = false);

// Set a voxel with sub-pixel (smooth / anti-aliased) positioning
void setPixelSmooth3D(float x, float y, float z, Color color);

// Read back a voxel colour
Color getPixel3D(int x, int y, int z);

// 3D line between two voxels
void drawLine3D(int x1, int y1, int z1, int x2, int y2, int z2, Color col);
void drawLine3D(Vector3i start, Vector3i end, Color col);
```

#### 2D Drawing Methods (per face)

All 2D methods take a `ScreenNumber` to identify which face to draw on.

```cpp
// Line between two points on a face
void drawLine2D(ScreenNumber screenNr, int x0, int y0, int x1, int y1, Color col);

// Circle outline
void drawCircle2D(ScreenNumber screenNr, int x0, int y0, int radius, Color col);

// Rectangle (outline or filled)
void drawRect2D(ScreenNumber screenNr, int x0, int y0, int x1, int y1, Color col,
                bool filled = false, Color fillColor = Color::black());

// Rectangle defined by centre + dimensions
void drawRectCentered2D(ScreenNumber screenNr, int x0, int y0, int w, int h, Color col,
                        bool filled = false, Color fillColor = Color::black());

// Text rendering (6px bitmap font)
void drawText(ScreenNumber screenNr, Vector2i topLeftPoint, Color col, std::string text);

// 1-bit bitmap sprite
void drawBitmap1bpp(ScreenNumber screenNr, Vector2i topLeftPoint, Color col, Bitmap1bpp bitmap);

// Image (loaded via the Image class)
void drawImage(ScreenNumber screenNr, Vector2i topLeftPoint, Image& image, Vector2i imageStartPoint);
```

#### Surface / Geometry Helpers

```cpp
// Check where a 3D point falls on the cube surface
bool isOnSurface(Vector3i point);
ScreenNumber getScreenNumber(Vector3i point);
bool isOnEdge(Vector3i point);
EdgeNumber getEdgeNumber(Vector3i point);
bool isOnCorner(Vector3i point);
CornerNumber getCornerNumber(Vector3i point);

// Get a 3D world position from a 2D screen coordinate
Vector3i getPointOnScreen(ScreenNumber screenNr, Vector2i point);

// Get a random 3D point on a specific face
Vector3i getRandomPointOnScreen(ScreenNumber screenNr);
```

#### MatrixApplication Methods (inherited)

```cpp
void start();           // Start the application (called once in main())
void stop();            // Stop the application loop
bool pause();           // Pause rendering
bool resume();          // Resume after pause

void setFps(int fps);   // Change the frame rate at runtime
int  getFps();

void setBrightness(int b); // 0–100
int  getBrightness();

AppState getAppState(); // starting | running | paused | ended | killed | failure
float    getLoad();     // CPU load factor (0.0 = idle, 1.0 = 100%)
```

#### Useful Constants

| Constant | Value | Description |
|---|---|---|
| `CUBESIZE` | `64` | Pixels per face edge |
| `CUBEMAXINDEX` | `63` | Maximum valid coordinate index |
| `CUBECENTER` | `32` | Centre coordinate |
| `DEFAULTFPS` | `40` | Default frame rate |
| `DEFAULTSERVERURI` | `"tcp://127.0.0.1:2017"` | Default server address |

---

### Dynamic Parameters (AnimationParams)

The framework provides a built-in parameter system (`params`, a `matrixserver::AnimationParams` member) that lets you expose tunable values to the **CubeWebapp UI** without recompiling. Parameters are registered in your constructor and read inside `loop()`.

#### Registering Parameters

Call these in your constructor:

```cpp
// Float slider
params.registerFloat("speed", "Speed", /*min*/ 0.1f, /*max*/ 5.0f, /*default*/ 1.0f, /*step*/ 0.1f, "group");

// Integer slider
params.registerInt("count", "Particle Count", /*min*/ 1, /*max*/ 200, /*default*/ 50, "group");

// Boolean toggle
params.registerBool("rainbow", "Rainbow Mode", /*default*/ true, "group");

// Dropdown / enum
params.registerEnum("mode", "Mode", {"Solid", "Fade", "Strobe"}, /*default*/ "Solid", "group");
```

#### Reading Parameters in `loop()`

```cpp
bool loop() override {
    float speed  = params.getFloat("speed");
    int   count  = params.getInt("count");
    bool  rainbow = params.getBool("rainbow");
    std::string mode = params.getString("mode");
    // ... use them ...
    render();
    return true;
}
```

The CubeWebapp will automatically generate the appropriate sliders, toggles, and dropdowns based on the registered schema. Users can also save and load parameter sets as JSON **Presets** in the UI.

---

## Joystick Input

The framework ships a `Joystick` class (and a convenience `JoystickManager`) that reads standard Linux joystick devices (`/dev/input/js*`). It runs an internal background thread, so your `loop()` just reads the latest state.

### CMakeLists.txt — no extra changes needed

`Joystick` is part of `libmatrixapplication` and is already linked via `matrixapplication::matrixapplication`.

### Header

```cpp
#include <Joystick.h>
```

### Joystick API

| Method | Returns | Description |
|---|---|---|
| `Joystick(int n)` | — | Open `/dev/input/js<n>` (0 = first joystick) |
| `Joystick(std::string path)` | — | Open a specific device path |
| `isFound()` | `bool` | `true` if the device was successfully opened |
| `getAxis(unsigned int n)` | `float` | Current axis value in **[−1.0, +1.0]** |
| `getAxisPress(unsigned int n)` | `float` | Axis value since last `clearAllButtonPresses()` |
| `getButton(unsigned int n)` | `bool` | Current button state (held) |
| `getButtonPress(unsigned int n)` | `bool` | `true` once per physical press; cleared after `clearAllButtonPresses()` |
| `clearAllButtonPresses()` | `void` | Reset all "press" flags — call this once per frame |

`JoystickManager` wraps multiple `Joystick` instances and exposes the same query API across all of them:

```cpp
JoystickManager jm(4); // open js0..js3
jm.getButtonPress(0);  // true if any joystick has button 0 pressed
jm.clearAllButtonPresses();
```

### Example — reading a joystick in your app

**`MyApp.h`**
```cpp
#include <CubeApplication.h>
#include <Joystick.h>

class MyApp : public CubeApplication {
public:
    MyApp(std::string serverUri);
    bool loop() override;
private:
    Joystick joystick;  // /dev/input/js0
};
```

**`MyApp.cpp`**
```cpp
MyApp::MyApp(std::string serverUri)
    : CubeApplication(40, serverUri, "MyApp"), joystick(0) {}

bool MyApp::loop() {
    if (joystick.isFound()) {
        float axis0 = joystick.getAxis(0);    // left stick X  (−1 to +1)
        float axis1 = joystick.getAxis(1);    // left stick Y

        if (joystick.getButtonPress(0)) {
            // react to button A press
        }
        joystick.clearAllButtonPresses();    // always clear at end of frame
    }

    // ... draw ...
    render();
    return true;
}
```

> **Hardware note:** Joystick devices (`/dev/input/js*`) are only available on Linux (Raspberry Pi or desktop). The `isFound()` guard ensures the app continues running gracefully when no controller is connected — you can add an AI/demo mode as a fallback (see the Snake example application).

---

## IMU Input (MPU-6050)

The framework provides a `Mpu6050` helper class for reading data from a **GY-521 / MPU-6050** inertial measurement unit connected via I²C on the Raspberry Pi.

> **Simulator tip:** IMU input also works without any hardware sensor. Open the CubeWebapp (`https://<your-machine-ip>:5173`) on a **smartphone** — the webapp reads the phone's built-in accelerometer/gyroscope via the browser's DeviceMotion API and forwards the orientation data to the matrixserver, which then becomes available through the same `MatrixApplication` IMU statics used by `Mpu6050`. This lets you test tilt-reactive apps in the simulator just by moving your phone.

### CMakeLists.txt

`Mpu6050` is part of `libmatrixapplication`. No extra link flags are required.

### Header

```cpp
#include <Mpu6050.h>
```

### Mpu6050 API

| Method | Returns | Description |
|---|---|---|
| `init()` | `void` | Open the I²C device and start the background refresh thread |
| `getAcceleration()` | `Eigen::Vector3f` | Raw acceleration vector in g-units (x, y, z) |
| `getCubeAccIntersect()` | `Eigen::Vector3i` | Maps the acceleration direction to a surface voxel on the cube (ready to pass to `setPixel3D`) |

The sensor is read in a background thread — your `loop()` always gets the latest value without blocking.

### Example — gravity-reactive pixel

**`MyApp.h`**
```cpp
#include <CubeApplication.h>
#include <Mpu6050.h>

class MyApp : public CubeApplication {
public:
    MyApp(std::string serverUri);
    bool loop() override;
private:
    Mpu6050 imu;
};
```

**`MyApp.cpp`**
```cpp
MyApp::MyApp(std::string serverUri)
    : CubeApplication(30, serverUri, "MyApp") {
    imu.init();  // start I2C reads
}

bool MyApp::loop() {
    fade(0.85f);  // leave a short trail

    // getCubeAccIntersect() maps the gravity vector to the nearest
    // surface pixel — the pixel follows the "down" side of the cube.
    setPixel3D(imu.getCubeAccIntersect(), Color::green());

    render();
    return true;
}
```

> **Hardware note:** The MPU-6050 is accessed via `/dev/i2c-*`. Make sure I²C is enabled (`raspi-config` → Interfaces → I2C) and the user running the app belongs to the `i2c` group.

---

## Audio Input

Audio data is delivered to your app through two independent sources — both feed the exact same static API, so your application code works unchanged regardless of which source is active:

| Source | How it works |
|---|---|
| **Browser microphone** | The CubeWebapp captures audio from the device's microphone (after the user grants permission) and forwards the processed volume and frequency data to the matrixserver over WebSocket. Works in the simulator on any device — desktop, laptop, or smartphone. |
| **Physical microphone (hardware)** | On the Raspberry Pi, the matrixserver reads audio directly from the system's default **ALSA** input device (e.g. a USB microphone or I²S mic hat). No browser is required — the server captures audio locally and distributes it to all connected apps. |

This makes it straightforward to build **audio-reactive animations** that respond in real time to music, speech, or ambient sound — both during development in the simulator and on the final LED Cube hardware.

The data arrives asynchronously and is stored in two **static** members of `MatrixApplication`, protected by a mutex. No extra library or constructor changes are needed; the data is always available as long as the server is running.

### Available data

| Static member | Type | Description |
|---|---|---|
| `MatrixApplication::latestAudioVolume` | `float` | Overall volume/RMS (roughly 0.0 – 1.0) |
| `MatrixApplication::latestAudioFrequencies` | `std::vector<float>` | Per-band normalised frequency amplitudes (FFT buckets, 0.0 – 1.0) |
| `MatrixApplication::audioDataMutex` | `std::mutex` | Lock this before reading either field |

### Example — audio-reactive spawn rate

```cpp
#include <MatrixApplication.h>  // for the static statics
#include <mutex>

bool MyApp::loop() {
    // Safely snapshot the latest audio data
    float volume = 0.0f;
    std::vector<float> freqs;
    {
        std::lock_guard<std::mutex> lock(MatrixApplication::audioDataMutex);
        volume = MatrixApplication::latestAudioVolume;
        freqs  = MatrixApplication::latestAudioFrequencies;
    }

    // Use volume to drive brightness
    float brightness = 0.2f + volume * 0.8f;

    // Use low-frequency band (bass) if available
    float bass = (!freqs.empty()) ? freqs[0] : 0.0f;

    // Example: expand a sphere radius with bass
    int radius = 10 + (int)(bass * 20.0f);

    drawCircle2D(front, 32, 32, radius, Color::blue());
    render();
    return true;
}
```

### Registering tunable audio parameters

It is a good practice to expose audio sensitivity thresholds through `AnimationParams` so they can be tweaked from the CubeWebapp without recompiling:

```cpp
// In constructor:
params.registerFloat("audioThreshold", "Audio Vol Threshold", 0.0f, 1.0f, 0.1f, 0.01f, "Audio");
params.registerFloat("audioSpeedMult", "Speed Multiplier",    0.0f, 5.0f, 2.0f, 0.1f,  "Audio");
params.registerBool ("audioColorShift", "Color Shift on Beat", true, "Audio");

// In loop():
float threshold  = params.getFloat("audioThreshold");
float speedMult  = params.getFloat("audioSpeedMult");
bool  colorShift = params.getBool ("audioColorShift");
```

> **Simulator (browser) note:** Audio capture in the CubeWebapp requires the browser to grant microphone permission. Open `https://localhost:5173`, click the microphone icon in the toolbar, and allow access. On physical hardware, the matrixserver reads audio from the system's default ALSA input device.

---

## Renaming the Template

To rename the template for your own application, update the following places:

1. **`CMakeLists.txt`** — change the `project()` name and the target name in `add_executable()`:
   ```cmake
   project(MyAwesomeApp VERSION 1.0.0)
   # ...
   set(MAINSRC src/MyAwesomeApp.cpp src/MyAwesomeApp.h src/main.cpp)
   add_executable(MyAwesomeApp ${MAINSRC})
   target_link_libraries(MyAwesomeApp ${MAINLIBS} ${Boost_LIBRARIES})
   ```

2. **`src/`** — rename `AppTemplate.h` / `AppTemplate.cpp` and update the class name inside them.

3. **`src/main.cpp`** — update the `#include` and instantiation to use your new class name.

4. **`CubeApplication` constructor** — update the `appName` string (third parameter). This name appears in the CubeWebapp's parameter panel.


