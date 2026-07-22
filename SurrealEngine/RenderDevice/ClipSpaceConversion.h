#pragma once

#include <array>

// WebXR supplies WebGL projection matrices whose normalized device depth is
// [-1, 1]. Surreal's WebGPU renderer consumes [0, 1]. Matrices use the WebXR
// column-major layout and multiply column vectors.
std::array<float, 16> ConvertProjectionDepthMinusOneToOneToZeroToOne(const std::array<float, 16>& projection);
