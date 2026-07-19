#pragma once

#include <string>

// WGSL port of the D3D11 Scene.vert/Scene.frag HLSL pair (D3D11FileResource.cpp).
// D3D11 is the porting source (not Vulkan's GLSL) because it already binds
// textures in the same fixed-4-slot shape WGSL needs - see
// WEBXR_IMPLEMENTATION_PLAN.md M2 for the full rationale.
//
// Dropped relative to D3D11's version (confirmed dead for this milestone):
//   - SV_ClipDistance0 (near-clip plane): core WGSL has no user clip planes.
//   - The hit-testing MRT output / HitIndex uniform: PushHit/PopHit are
//     no-ops on this backend (editor-only, already excluded from the
//     Emscripten build), so the whole hit-buffer path is unreachable.
class WebGPUShaders
{
public:
	static std::string GetSceneShaderSource();
};
