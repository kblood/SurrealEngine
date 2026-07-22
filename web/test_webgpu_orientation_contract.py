#!/usr/bin/env python3
"""Source-level regression checks for the WebGPU canvas/WebXR Y contract.

The physical compositor cannot be exercised in CI, so these checks protect
the explicit convention plumbing that keeps a desktop canvas correction from
being applied a second time to a browser-owned WebXR projection layer.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def source(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


class WebGPUOrientationContractTests(unittest.TestCase):
    def test_contract_has_explicit_opposite_signs(self):
        header = source("SurrealEngine/RenderDevice/RenderDevice.h")
        self.assertIn("EngineProjection = 0", header)
        self.assertIn("NativeWebGPUProjectionLayer = 1", header)
        self.assertIn(
            "NativeWebGPUProjectionLayer ? 1.0f : -1.0f", header
        )

    def test_shader_uses_uniform_sign_not_an_unconditional_flip(self):
        shader = source("SurrealEngine/RenderDevice/WebGPU/WebGPUShaders.cpp")
        self.assertIn("clipSpaceYSign: f32", shader)
        self.assertIn("output.pos.y *= uniforms.clipSpaceYSign", shader)
        self.assertNotIn("output.pos.y = -output.pos.y", shader)

    def test_desktop_defaults_to_engine_projection_convention(self):
        header = source("SurrealEngine/RenderDevice/RenderDevice.h")
        canvas = source("SurrealEngine/Render/RenderCanvas.cpp")
        self.assertRegex(
            header,
            r"ClipSpaceYConvention\s*=\s*WebGPUClipSpaceYConvention::EngineProjection",
        )
        self.assertIn(
            "Canvas.Frame.ClipSpaceYConvention = "
            "WebGPUClipSpaceYConvention::EngineProjection",
            canvas,
        )

    def test_webxr_view_is_explicitly_native_webgpu(self):
        bridge = source("SurrealEngine/WebXR/WebXRFrameBridge.cpp")
        self.assertRegex(
            bridge,
            r"target\.ClipSpaceYConvention\s*=\s*"
            r"WebGPUClipSpaceYConvention::NativeWebGPUProjectionLayer",
        )

    def test_world_portals_weapon_and_hud_propagate_eye_convention(self):
        scene = source("SurrealEngine/Render/RenderScene.cpp")
        visible = source("SurrealEngine/Render/VisibleFrame.cpp")
        canvas = source("SurrealEngine/Render/RenderCanvas.cpp")
        self.assertIn(
            "viewport.ClipSpaceYConvention = view.ClipSpaceYConvention", scene
        )
        self.assertIn(
            "Canvas.Frame.ClipSpaceYConvention = view.ClipSpaceYConvention", scene
        )
        self.assertIn(
            "subViewport.ClipSpaceYConvention = Frame.ClipSpaceYConvention", visible
        )
        self.assertIn(
            "Canvas.Frame.ClipSpaceYConvention = MainFrame.Frame.ClipSpaceYConvention",
            canvas,
        )

    def test_hud_projection_maps_native_ndc_top_to_framebuffer_top(self):
        scene = source("SurrealEngine/Render/RenderScene.cpp")
        self.assertIn("WebGPUFramebufferYFromNDC(view.ClipSpaceYConvention", scene)
        # For native WebGPU, NDC +1 is framebuffer y=0 and NDC -1 is y=height.
        native = lambda ndc_y: (1.0 - ndc_y) * 0.5
        self.assertEqual(native(1.0), 0.0)
        self.assertEqual(native(-1.0), 1.0)

    def test_uniform_layout_keeps_wgsl_alignment(self):
        header = source(
            "SurrealEngine/RenderDevice/WebGPU/WebGPUPipelineCache.h"
        )
        self.assertIn("float ClipSpaceYSign", header)
        self.assertIn("float Padding[3]", header)
        self.assertIn("static_assert(sizeof(WebGPUSceneUniforms) == 80)", header)


if __name__ == "__main__":
    unittest.main()
