#ifndef SANDCUBE_H
#define SANDCUBE_H

#include <CubeApplication.h>

#include <memory>
#include <string>

#include "ImuOrientation.h"
#include "KeyboardTilt.h"
#include "SandEngine.h"
#include "SandSpawner.h"

// ──────────────────────────────────────────────────────────────────────────
//  SandCube
//
//  A 64³ falling-sand cellular automaton rendered on all six faces of an
//  LED cube via the matrixserver libmatrixapplication framework.
//
//  Gravity comes from one of two OrientationSource implementations:
//    • ImuOrientation — matrixserver Mpu6050 helper (hardware IMU, or
//      smartphone DeviceMotion forwarded by the CubeWebapp).
//    • KeyboardTilt   — WASD keys on stdin (raw terminal mode).
//
//  The active source is chosen at runtime from the CubeWebapp parameter
//  panel (params "orientation" enum).
// ──────────────────────────────────────────────────────────────────────────
class SandCube : public CubeApplication {
public:
    explicit SandCube(std::string serverUri, bool imuDebug = false);
    bool loop() override;

private:
    void renderSurface();
    OrientationSource& activeSource();

    SandEngine     engine_;
    SandSpawner    spawner_;
    ImuOrientation imu_;
    std::unique_ptr<KeyboardTilt> keyboard_;  // lazy: only constructed if selected

    int     frame_        = 0;
    bool    imuDebug_     = false;
    Gravity lastGravity_;
};

#endif // SANDCUBE_H
