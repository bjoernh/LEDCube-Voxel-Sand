#ifndef WATERCUBE_H
#define WATERCUBE_H

#include <CubeApplication.h>

#include <memory>
#include <string>

#include "FluidEngine.h"
#include "ImuOrientation.h"
#include "KeyboardTilt.h"

// ──────────────────────────────────────────────────────────────────────────
//  WaterCube
//
//  PIC/FLIP water simulation running on all six faces of the 64³ LED cube
//  via the matrixserver libmatrixapplication framework.  Mirrors SandCube
//  in structure but drives a FluidEngine instead of SandEngine.
//
//  Gravity source and runtime parameters are identical in style to SandCube.
// ──────────────────────────────────────────────────────────────────────────
class WaterCube : public CubeApplication {
public:
    explicit WaterCube(std::string serverUri, bool imuDebug = false, bool profile = false);
    bool loop() override;

private:
    OrientationSource& activeSource();

    FluidEngine    engine_;
    ImuOrientation imu_;
    std::unique_ptr<KeyboardTilt> keyboard_;

    Gravity lastGravity_;
    int     frame_               = 0;
    bool    imuDebug_            = false;
    bool    lastRefillWasActive_ = false;
};

#endif // WATERCUBE_H
