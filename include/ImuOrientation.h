#pragma once

#include "OrientationSource.h"

#include <Mpu6050.h>

// ──────────────────────────────────────────────────────────────────────────
//  ImuOrientation
//
//  OrientationSource implementation backed by the matrixserver Mpu6050
//  helper.  The underlying sensor is refreshed in a background thread by
//  libmatrixapplication, so getGravity() is a cheap, non-blocking read.
//
//  Simulator note: the CubeWebapp forwards DeviceMotion data from a
//  browser/phone into the same Mpu6050 statics, so this works both on
//  the physical cube (real I²C MPU-6050) and in the Docker simulator.
// ──────────────────────────────────────────────────────────────────────────
class ImuOrientation : public OrientationSource {
public:
    ImuOrientation();

    [[nodiscard]] Gravity getGravity() const noexcept override;

private:
    mutable Mpu6050 imu_;
};
