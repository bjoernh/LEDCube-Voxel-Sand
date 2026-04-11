#include "ImuOrientation.h"

#include <Eigen/Core>
#include <cmath>

ImuOrientation::ImuOrientation() {
    imu_.init();
}

// ──────────────────────────────────────────────────────────────────────────
//  Mpu6050::getAcceleration() returns an Eigen::Vector3f in g-units. The
//  matrixserver convention (see getCubeAccIntersect, which "maps the
//  acceleration direction to a surface voxel on the down side of the
//  cube") is that this vector already points in the current "down"
//  direction relative to the cube — i.e. in the direction of gravity.
//
//  We normalise it and return it directly as the Gravity vector;
//  SandEngine::rebuildSlideDirs() turns the continuous vector into a
//  discrete primary move direction plus a sorted list of diagonal slides.
//
//  If the sensor is unavailable (getAcceleration returns a zero vector),
//  fall back to the default "-Y" gravity so the app still runs without
//  hardware.
// ──────────────────────────────────────────────────────────────────────────
Gravity ImuOrientation::getGravity() const noexcept {
    const Eigen::Vector3f a = imu_.getAcceleration();

    const float mag2 = a.squaredNorm();
    if (mag2 < 1e-6f) {
        return Gravity{0.0f, -1.0f, 0.0f};
    }

    const float inv = 1.0f / std::sqrt(mag2);
    return Gravity{a.x() * inv, a.y() * inv, a.z() * inv};
}
