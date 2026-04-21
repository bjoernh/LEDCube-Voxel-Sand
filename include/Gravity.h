#pragma once

// ──────────────────────────────────────────────────────────────────────────
//  Gravity
//
//  Continuous 3-D "down" unit vector.  Both SandEngine (snaps to a discrete
//  26-neighbour direction internally) and FluidEngine (uses the float vector
//  directly for body-force application) consume this type.
//
//  Defaults to straight-down (-Y) so the app works without a live sensor.
// ──────────────────────────────────────────────────────────────────────────
struct Gravity {
    float x = 0.0f;
    float y = 0.0f;
    float z = 1.0f;   // z+ = downward in the setPixel3D coordinate system
};
