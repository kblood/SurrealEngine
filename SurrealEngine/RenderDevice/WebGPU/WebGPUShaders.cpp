
#include "Precomp.h"
#include "WebGPUShaders.h"

std::string WebGPUShaders::GetSceneShaderSource()
{
	return R"(
struct Uniforms
{
	objectToProjection: mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@group(1) @binding(0) var samplerTex: sampler;
@group(1) @binding(1) var samplerTexLightmap: sampler;
@group(1) @binding(2) var samplerTexMacro: sampler;
@group(1) @binding(3) var samplerTexDetail: sampler;
@group(1) @binding(4) var tex: texture_2d<f32>;
@group(1) @binding(5) var texLightmap: texture_2d<f32>;
@group(1) @binding(6) var texMacro: texture_2d<f32>;
@group(1) @binding(7) var texDetail: texture_2d<f32>;

struct VertexInput
{
	@location(0) flags: u32,
	@location(1) position: vec3<f32>,
	@location(2) texCoord: vec2<f32>,
	@location(3) texCoord2: vec2<f32>,
	@location(4) texCoord3: vec2<f32>,
	@location(5) texCoord4: vec2<f32>,
	@location(6) color: vec4<f32>,
};

struct VertexOutput
{
	@builtin(position) pos: vec4<f32>,
	@location(0) @interpolate(flat) flags: u32,
	@location(1) texCoord: vec2<f32>,
	@location(2) texCoord2: vec2<f32>,
	@location(3) texCoord3: vec2<f32>,
	@location(4) texCoord4: vec2<f32>,
	@location(5) color: vec4<f32>,
};

@vertex
fn vs_main(input: VertexInput) -> VertexOutput
{
	var output: VertexOutput;
	output.pos = uniforms.objectToProjection * vec4<f32>(input.position, 1.0);
	// SurrealEngine's projection matrices use the D3D/Vulkan screen-Y
	// convention. WebGPU's clip-space Y maps to the canvas in the opposite
	// direction, so correct the clip position once for every draw path. The
	// old per-path texture-V mirrors made texture content readable but left
	// world geometry and UI placement vertically inverted.
	output.pos.y = -output.pos.y;
	output.flags = input.flags;
	output.texCoord = input.texCoord;
	output.texCoord2 = input.texCoord2;
	output.texCoord3 = input.texCoord3;
	output.texCoord4 = input.texCoord4;
	output.color = input.color;
	return output;
}

override alphaTest: bool = false;

fn darkClamp(c: vec4<f32>) -> vec4<f32>
{
	// Make all textures a little darker as some of the textures (i.e coronas)
	// never become completely black as they should have.
	let cutoff = 3.1 / 255.0;
	return vec4<f32>(clamp((c.rgb - cutoff) / (1.0 - cutoff), vec3<f32>(0.0), vec3<f32>(1.0)), c.a);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32>
{
	let actorXBlending = select(1.0, 1.5, (input.flags & 32u) != 0u);
	let oneXBlending = select(2.0, 1.0, (input.flags & 64u) != 0u);

	var outColor = darkClamp(textureSample(tex, samplerTex, input.texCoord)) * input.color;
	outColor = vec4<f32>(outColor.rgb * actorXBlending, outColor.a);

	// textureSample() uses implicit derivatives and WGSL requires that to
	// only happen in uniform control flow (unlike HLSL's Sample(), which
	// D3D11FileResource.cpp's original shader called the same way inside
	// these same per-pixel-flag-dependent branches without complaint).
	// textureSampleLevel() with an explicit LOD sidesteps that restriction;
	// these are all low-frequency auxiliary textures (macro/lightmap/detail/
	// fogmap) where forcing LOD 0 is visually indistinguishable.
	if ((input.flags & 2u) != 0u) // Macro texture
	{
		outColor = outColor * darkClamp(textureSampleLevel(texMacro, samplerTexMacro, input.texCoord3, 0.0));
	}

	if ((input.flags & 1u) != 0u) // Lightmap
	{
		outColor = vec4<f32>(outColor.rgb * textureSampleLevel(texLightmap, samplerTexLightmap, input.texCoord2, 0.0).rgb * oneXBlending, outColor.a);
	}

	if ((input.flags & 4u) != 0u) // Detail texture
	{
		let fadedistance = 380.0;
		let a = clamp(2.0 - (1.0 / input.pos.w) / fadedistance, 0.0, 1.0);
		let detailColor = (textureSampleLevel(texDetail, samplerTexDetail, input.texCoord4, 0.0) - 0.5) * 0.5 + 1.0;
		outColor = vec4<f32>(mix(outColor.rgb, outColor.rgb * detailColor.rgb, a), outColor.a);
	}
	else if ((input.flags & 8u) != 0u) // Fog map
	{
		let fogcolor = textureSampleLevel(texDetail, samplerTexDetail, input.texCoord4, 0.0);
		outColor = vec4<f32>(fogcolor.rgb + outColor.rgb * (1.0 - fogcolor.a), outColor.a);
	}
	else if ((input.flags & 16u) != 0u) // Fog color
	{
		let fogcolor = vec4<f32>(input.texCoord2, input.texCoord3);
		outColor = vec4<f32>(fogcolor.rgb + outColor.rgb * (1.0 - fogcolor.a), outColor.a);
	}

	if (alphaTest && outColor.a < 0.5)
	{
		discard;
	}

	return clamp(outColor, vec4<f32>(0.0), vec4<f32>(1.0));
}
)";
}
