#include "Precomp.h"
#include "WebGL2RenderDevice.h"

#include "UObject/ULevel.h"

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <surrealwidgets/core/widget.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace
{
	WebGL2RenderDevice* ActiveDevice = nullptr;

	const char* VertexShaderSource = R"GLSL(#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in uint inFlags;
layout(location = 1) in vec3 inPosition;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec2 inTexCoord2;
layout(location = 4) in vec2 inTexCoord3;
layout(location = 5) in vec2 inTexCoord4;
layout(location = 6) in vec4 inColor;

uniform mat4 objectToProjection;
uniform bool webXRProjection;

flat out uint flags;
out vec2 texCoord;
out vec2 texCoord2;
out vec2 texCoord3;
out vec2 texCoord4;
out vec4 vertexColor;

void main()
{
	gl_Position = objectToProjection * vec4(inPosition, 1.0);
	// The flat engine matrices use D3D/Vulkan's top-left screen convention
	// and zero-to-one clip depth. WebXR matrices already use WebGL clip space;
	// the switch is present now so XR can reuse this exact draw path later.
	if (!webXRProjection)
	{
		gl_Position.y = -gl_Position.y;
		gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;
	}
	flags = inFlags;
	texCoord = inTexCoord;
	texCoord2 = inTexCoord2;
	texCoord3 = inTexCoord3;
	texCoord4 = inTexCoord4;
	vertexColor = inColor;
}
)GLSL";

	const char* FragmentShaderSource = R"GLSL(#version 300 es
precision highp float;
precision highp int;

flat in uint flags;
in vec2 texCoord;
in vec2 texCoord2;
in vec2 texCoord3;
in vec2 texCoord4;
in vec4 vertexColor;

uniform sampler2D tex;
uniform sampler2D texLightmap;
uniform sampler2D texMacro;
uniform sampler2D texDetail;
uniform bool alphaTest;

out vec4 outFragment;

vec4 darkClamp(vec4 color)
{
	const float cutoff = 3.1 / 255.0;
	return vec4(clamp((color.rgb - vec3(cutoff)) / (1.0 - cutoff), vec3(0.0), vec3(1.0)), color.a);
}

void main()
{
	float actorXBlending = (flags & 32u) != 0u ? 1.5 : 1.0;
	float oneXBlending = (flags & 64u) != 0u ? 1.0 : 2.0;
	vec4 color = darkClamp(texture(tex, texCoord)) * vertexColor;
	color.rgb *= actorXBlending;

	if ((flags & 2u) != 0u)
		color *= darkClamp(textureLod(texMacro, texCoord3, 0.0));
	if ((flags & 1u) != 0u)
		color.rgb *= textureLod(texLightmap, texCoord2, 0.0).rgb * oneXBlending;

	if ((flags & 4u) != 0u)
	{
		const float fadeDistance = 380.0;
		float distanceToPixel = 1.0 / max(gl_FragCoord.w, 0.000001);
		float amount = clamp(2.0 - distanceToPixel / fadeDistance, 0.0, 1.0);
		vec4 detailColor = (textureLod(texDetail, texCoord4, 0.0) - 0.5) * 0.5 + 1.0;
		color.rgb = mix(color.rgb, color.rgb * detailColor.rgb, amount);
	}
	else if ((flags & 8u) != 0u)
	{
		vec4 fogColor = textureLod(texDetail, texCoord4, 0.0);
		color.rgb = fogColor.rgb + color.rgb * (1.0 - fogColor.a);
	}
	else if ((flags & 16u) != 0u)
	{
		vec4 fogColor = vec4(texCoord2, texCoord3);
		color.rgb = fogColor.rgb + color.rgb * (1.0 - fogColor.a);
	}

	if (alphaTest && color.a < 0.5)
		discard;
	outFragment = clamp(color, vec4(0.0), vec4(1.0));
}
)GLSL";

	void SetVertex(WebGL2SceneVertex& vertex, uint32_t flags, vec3 position, vec2 uv, vec2 uv2, vec2 uv3, vec2 uv4, vec4 color)
	{
		vertex.Flags = flags;
		vertex.Position[0] = position.x; vertex.Position[1] = position.y; vertex.Position[2] = position.z;
		vertex.TexCoord[0] = uv.x; vertex.TexCoord[1] = uv.y;
		vertex.TexCoord2[0] = uv2.x; vertex.TexCoord2[1] = uv2.y;
		vertex.TexCoord3[0] = uv3.x; vertex.TexCoord3[1] = uv3.y;
		vertex.TexCoord4[0] = uv4.x; vertex.TexCoord4[1] = uv4.y;
		vertex.Color[0] = color.x; vertex.Color[1] = color.y; vertex.Color[2] = color.z; vertex.Color[3] = color.w;
	}
}

WebGL2RenderDevice::WebGL2RenderDevice(Widget* viewport)
{
	Viewport = viewport;
	context = std::make_unique<WebGL2Context>();
	InitializeGeneration();
	ActiveDevice = this;
}

WebGL2RenderDevice::~WebGL2RenderDevice()
{
	if (ActiveDevice == this)
		ActiveDevice = nullptr;
	const bool canDelete = context && context->IsReady() && context->MakeCurrent();
	if (textures)
	{
		textures->Clear(canDelete);
		textures.reset();
	}
	ReleaseResources(canDelete);
}

bool WebGL2RenderDevice::EnsureReady()
{
	if (!context || !context->IsReady() || !context->MakeCurrent())
		return false;
	if (initializedGeneration != context->Generation())
		InitializeGeneration();
	return resourcesReady && initializedGeneration == context->Generation();
}

GLuint WebGL2RenderDevice::CompileShader(GLenum type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);
	GLint compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (compiled != GL_TRUE)
	{
		glDeleteShader(shader);
		errorCount++;
		return 0;
	}
	return shader;
}

bool WebGL2RenderDevice::CreateResources()
{
	vertexShader = CompileShader(GL_VERTEX_SHADER, VertexShaderSource);
	fragmentShader = CompileShader(GL_FRAGMENT_SHADER, FragmentShaderSource);
	if (!vertexShader || !fragmentShader)
		return false;
	program = glCreateProgram();
	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glLinkProgram(program);
	GLint linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (linked != GL_TRUE)
	{
		errorCount++;
		return false;
	}
	glUseProgram(program);
	matrixUniform = glGetUniformLocation(program, "objectToProjection");
	alphaTestUniform = glGetUniformLocation(program, "alphaTest");
	xrProjectionUniform = glGetUniformLocation(program, "webXRProjection");
	glUniform1i(glGetUniformLocation(program, "tex"), 0);
	glUniform1i(glGetUniformLocation(program, "texLightmap"), 1);
	glUniform1i(glGetUniformLocation(program, "texMacro"), 2);
	glUniform1i(glGetUniformLocation(program, "texDetail"), 3);

	glGenVertexArrays(1, &vertexArray);
	glBindVertexArray(vertexArray);
	glGenBuffers(1, &vertexBuffer);
	glGenBuffers(1, &indexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
	glEnableVertexAttribArray(0);
	glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, sizeof(WebGL2SceneVertex), reinterpret_cast<const void*>(offsetof(WebGL2SceneVertex, Flags)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(WebGL2SceneVertex), reinterpret_cast<const void*>(offsetof(WebGL2SceneVertex, Position)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(WebGL2SceneVertex), reinterpret_cast<const void*>(offsetof(WebGL2SceneVertex, TexCoord)));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(WebGL2SceneVertex), reinterpret_cast<const void*>(offsetof(WebGL2SceneVertex, TexCoord2)));
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(WebGL2SceneVertex), reinterpret_cast<const void*>(offsetof(WebGL2SceneVertex, TexCoord3)));
	glEnableVertexAttribArray(5);
	glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, sizeof(WebGL2SceneVertex), reinterpret_cast<const void*>(offsetof(WebGL2SceneVertex, TexCoord4)));
	glEnableVertexAttribArray(6);
	glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(WebGL2SceneVertex), reinterpret_cast<const void*>(offsetof(WebGL2SceneVertex, Color)));

	glGenSamplers(4, samplers);
	for (int index = 0; index < 4; index++)
	{
		const bool nearest = (index & 1) != 0;
		const bool clamp = (index & 2) != 0;
		glSamplerParameteri(samplers[index], GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
		glSamplerParameteri(samplers[index], GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
		glSamplerParameteri(samplers[index], GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		glSamplerParameteri(samplers[index], GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
	}
	return true;
}

void WebGL2RenderDevice::ReleaseResources(bool deleteObjects)
{
	if (deleteObjects)
	{
		if (samplers[0] || samplers[1] || samplers[2] || samplers[3]) glDeleteSamplers(4, samplers);
		if (indexBuffer) glDeleteBuffers(1, &indexBuffer);
		if (vertexBuffer) glDeleteBuffers(1, &vertexBuffer);
		if (vertexArray) glDeleteVertexArrays(1, &vertexArray);
		if (program) glDeleteProgram(program);
		if (fragmentShader) glDeleteShader(fragmentShader);
		if (vertexShader) glDeleteShader(vertexShader);
	}
	program = vertexShader = fragmentShader = vertexArray = vertexBuffer = indexBuffer = 0;
	for (GLuint& sampler : samplers) sampler = 0;
	matrixUniform = alphaTestUniform = xrProjectionUniform = -1;
	resourcesReady = false;
}

void WebGL2RenderDevice::InitializeGeneration()
{
	if (!context || !context->IsReady() || !context->MakeCurrent())
		return;
	if (program || vertexShader || fragmentShader || vertexArray || vertexBuffer || indexBuffer)
		ReleaseResources(initializedGeneration == 0 || initializedGeneration == context->Generation());
	if (!textures)
		textures = std::make_unique<WebGL2TextureManager>(context->Generation());
	else
		textures->ResetForGeneration(context->Generation());

	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glClearDepthf(1.0f);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	resourcesReady = CreateResources();
	if (resourcesReady)
	{
		initializedGeneration = context->Generation();
		UpdateSceneUniforms(currentMatrix);
	}
	CountErrors();
}

void WebGL2RenderDevice::CountErrors()
{
	for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
	{
		// WebGL surfaces deliberate/real context loss through getError(). It is
		// lifecycle status, not a rendering fault. Browsers may expose either
		// WebGL's CONTEXT_LOST_WEBGL token or GLES' GL_CONTEXT_LOST token.
		if (error == 0x9242 || error == 0x0507)
			contextLossStatusCount++;
		else
			errorCount++;
	}
}

void WebGL2RenderDevice::CountUnsupportedDraw()
{
	unsupportedDrawCount++;
}

void WebGL2RenderDevice::UpdateSceneUniforms(const mat4& matrix, bool webXRProjection)
{
	currentMatrix = matrix;
	if (!program)
		return;
	glUseProgram(program);
	glUniformMatrix4fv(matrixUniform, 1, GL_FALSE, matrix.matrix);
	glUniform1i(xrProjectionUniform, webXRProjection ? 1 : 0);
}

void WebGL2RenderDevice::Flush(bool allowPrecache)
{
	if (!EnsureReady())
		return;
	glFlush();
	if (textures)
		textures->Clear(true);
	if (allowPrecache)
		PrecacheOnFlip = true;
}

void WebGL2RenderDevice::Lock(vec4 inFlashScale, vec4 inFlashFog, vec4 screenClear, uint8_t*, int* hitSize)
{
	if (hitSize)
		*hitSize = 0;
	locked = false;
	drawCallCount = 0;
	if (!EnsureReady())
	{
		suppressedFrameCount++;
		return;
	}

	int bufferWidth = 0;
	int bufferHeight = 0;
	if (!context->GetDrawingBufferSize(bufferWidth, bufferHeight) || bufferWidth <= 0 || bufferHeight <= 0)
	{
		suppressedFrameCount++;
		return;
	}
	currentWidth = bufferWidth;
	currentHeight = bufferHeight;
	flashScale = inFlashScale;
	flashFog = inFlashFog;
	currentFrame = nullptr;
	glViewport(0, 0, currentWidth, currentHeight);
	glDepthRangef(0.1f, 1.0f);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(screenClear.x, screenClear.y, screenClear.z, screenClear.w);
	glClearDepthf(1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	CountErrors();
	locked = true;
}

void WebGL2RenderDevice::Unlock(bool)
{
	if (!locked)
		return;
	glFlush();
	CountErrors();
	locked = false;
	frameCount++;
}

void WebGL2RenderDevice::ApplyPipelineState(uint32_t polyFlags, const vec4* blendColor)
{
	const bool subpixelFont = polyFlags == PF_SubpixelFont;
	if (subpixelFont || (polyFlags & (PF_Translucent | PF_Modulated | PF_Highlighted)))
	{
		glEnable(GL_BLEND);
		if (subpixelFont)
		{
			const vec4 color = blendColor ? *blendColor : vec4(1.0f);
			glBlendColor(color.x, color.y, color.z, color.w);
			glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
		else if (polyFlags & PF_Translucent)
			glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		else if (polyFlags & PF_Modulated)
			glBlendFuncSeparate(GL_DST_COLOR, GL_SRC_COLOR, GL_DST_ALPHA, GL_SRC_ALPHA);
		else
			glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
	{
		glDisable(GL_BLEND);
	}
	const GLboolean writeColor = (!subpixelFont && (polyFlags & PF_Invisible)) ? GL_FALSE : GL_TRUE;
	glColorMask(writeColor, writeColor, writeColor, writeColor);
	glDepthMask((!subpixelFont && (polyFlags & PF_Occlude)) ? GL_TRUE : GL_FALSE);
	glUniform1i(alphaTestUniform, (subpixelFont || (polyFlags & PF_Masked)) ? 1 : 0);
}

void WebGL2RenderDevice::BindTextureUnit(int unit, WebGL2CachedTexture* texture, bool noSmooth, bool clamp)
{
	if (!texture)
		texture = textures->GetNullTexture();
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, texture->Texture);
	glBindSampler(unit, samplers[(noSmooth ? 1 : 0) | (clamp ? 2 : 0)]);
}

void WebGL2RenderDevice::DrawIndexed(GLenum mode, const std::vector<WebGL2SceneVertex>& vertices, const std::vector<uint32_t>& indexes,
	uint32_t polyFlags, WebGL2CachedTexture* texture, WebGL2CachedTexture* lightmap, WebGL2CachedTexture* macroTexture,
	WebGL2CachedTexture* detailTexture, bool clampTexture, const vec4* blendColor, float minDepth, float maxDepth)
{
	if (!locked || vertices.empty() || indexes.empty() || !EnsureReady())
		return;
	glUseProgram(program);
	ApplyPipelineState(polyFlags, blendColor);
	const bool noSmooth = (polyFlags & PF_NoSmooth) != 0;
	BindTextureUnit(0, texture, noSmooth, clampTexture);
	BindTextureUnit(1, lightmap, false, false);
	BindTextureUnit(2, macroTexture, false, false);
	BindTextureUnit(3, detailTexture, false, false);
	glDepthRangef(minDepth, maxDepth);
	glBindVertexArray(vertexArray);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(WebGL2SceneVertex)), vertices.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indexes.size() * sizeof(uint32_t)), indexes.data(), GL_STREAM_DRAW);
	glDrawElements(mode, static_cast<GLsizei>(indexes.size()), GL_UNSIGNED_INT, nullptr);
	drawCallCount++;
	CountErrors();
}

void WebGL2RenderDevice::DrawComplexSurface(FSceneNode*, FSurfaceInfo& surface, FSurfaceFacet& facet)
{
	if (!locked || facet.VertexCount < 3)
		return;
	const uint32_t polyFlags = ApplyPrecedenceRules(surface.PolyFlags);
	ComplexSurfaceInfo info;
	info.Facet = &facet;
	info.Texture = textures->GetTexture(surface.Texture, (polyFlags & PF_Masked) != 0);
	info.Lightmap = textures->GetTexture(surface.LightMap, false);
	info.MacroTexture = textures->GetTexture(surface.MacroTexture, false);
	info.DetailTexture = textures->GetTexture(surface.DetailTexture, false);
	const bool validFog = surface.FogMap && surface.FogMap->NumMips > 0 && surface.FogMap->Mips && !surface.FogMap->Mips[0].Data.empty();
	info.FogMap = validFog ? textures->GetTexture(surface.FogMap, false) : textures->GetNullTexture();
	if (surface.DetailTexture && surface.FogMap)
		info.DetailTexture = textures->GetNullTexture();
	if (info.FogMap != textures->GetNullTexture())
		info.DetailTexture = info.FogMap;
	DrawComplexSurfaceFaces(info, polyFlags);
}

void WebGL2RenderDevice::DrawComplexSurfaceFaces(const ComplexSurfaceInfo& info, uint32_t polyFlags)
{
	WebGL2CachedTexture* nullTexture = textures->GetNullTexture();
	uint32_t flags = 0;
	if (info.Lightmap != nullTexture) flags |= 1;
	if (info.MacroTexture != nullTexture) flags |= 2;
	if (info.DetailTexture != nullTexture && info.FogMap == nullTexture) flags |= 4;
	if (info.FogMap != nullTexture) flags |= 8;
	if (LightMode == 1) flags |= 64;

	const vec3 xaxis = info.Facet->MapCoords.XAxis;
	const vec3 yaxis = info.Facet->MapCoords.YAxis;
	const float uDot = dot(xaxis, info.Facet->MapCoords.Origin);
	const float vDot = dot(yaxis, info.Facet->MapCoords.Origin);
	const float uPan = uDot + info.Texture->PanX;
	const float vPan = vDot + info.Texture->PanY;
	const float lmUPan = uDot + info.Lightmap->PanX - 0.5f * info.Lightmap->UScale;
	const float lmVPan = vDot + info.Lightmap->PanY - 0.5f * info.Lightmap->VScale;
	const float macroUPan = uDot + info.MacroTexture->PanX;
	const float macroVPan = vDot + info.MacroTexture->PanY;
	const float detailUPan = uDot + (info.FogMap == nullTexture ? info.DetailTexture->PanX : info.FogMap->PanX - 0.5f * info.FogMap->UScale);
	const float detailVPan = vDot + (info.FogMap == nullTexture ? info.DetailTexture->PanY : info.FogMap->PanY - 0.5f * info.FogMap->VScale);

	std::vector<WebGL2SceneVertex> vertices(info.Facet->VertexCount);
	for (uint32_t i = 0; i < info.Facet->VertexCount; i++)
	{
		const vec3 point = info.Facet->Vertices[i];
		const float u = dot(xaxis, point);
		const float v = dot(yaxis, point);
		SetVertex(vertices[i], flags, point,
			vec2((u - uPan) * info.Texture->UMult, (v - vPan) * info.Texture->VMult),
			vec2((u - lmUPan) * info.Lightmap->UMult, (v - lmVPan) * info.Lightmap->VMult),
			vec2((u - macroUPan) * info.MacroTexture->UMult, (v - macroVPan) * info.MacroTexture->VMult),
			vec2((u - detailUPan) * info.DetailTexture->UMult, (v - detailVPan) * info.DetailTexture->VMult), vec4(1.0f));
	}
	std::vector<uint32_t> indexes;
	indexes.reserve((info.Facet->VertexCount - 2) * 3);
	for (uint32_t i = 2; i < info.Facet->VertexCount; i++)
	{
		indexes.push_back(0); indexes.push_back(i - 1); indexes.push_back(i);
	}
	DrawIndexed(GL_TRIANGLES, vertices, indexes, polyFlags, info.Texture, info.Lightmap, info.MacroTexture, info.DetailTexture);
}

void WebGL2RenderDevice::DrawGouraudPolygon(FSceneNode*, FTextureInfo& info, const GouraudVertex* points, int pointCount, uint32_t polyFlags)
{
	if (!locked || !points || pointCount < 3)
		return;
	polyFlags = ApplyPrecedenceRules(polyFlags);
	WebGL2CachedTexture* texture = textures->GetTexture(&info, (polyFlags & PF_Masked) != 0);
	uint32_t flags = (polyFlags & (PF_RenderFog | PF_Translucent | PF_Modulated)) == PF_RenderFog ? 16 : 0;
	if ((polyFlags & (PF_Translucent | PF_Modulated)) == 0 && LightMode == 2) flags |= 32;
	const bool modulated = (polyFlags & PF_Modulated) != 0;
	std::vector<WebGL2SceneVertex> vertices(pointCount);
	for (int i = 0; i < pointCount; i++)
	{
		const GouraudVertex& point = points[i];
		const vec4 color = modulated ? vec4(1.0f) : vec4(point.Light.x, point.Light.y, point.Light.z, 1.0f);
		SetVertex(vertices[i], flags, point.Point, vec2(point.UV.x * texture->UMult, point.UV.y * texture->VMult),
			vec2(point.Fog.x, point.Fog.y), vec2(point.Fog.z, point.Fog.w), vec2(0.0f), color);
	}
	std::vector<uint32_t> indexes;
	indexes.reserve((pointCount - 2) * 3);
	for (uint32_t i = 2; i < static_cast<uint32_t>(pointCount); i++)
	{
		indexes.push_back(0); indexes.push_back(i - 1); indexes.push_back(i);
	}
	DrawIndexed(GL_TRIANGLES, vertices, indexes, polyFlags, texture);
}

void WebGL2RenderDevice::DrawTile(FSceneNode* frame, FTextureInfo& info, float x, float y, float width, float height,
	float u, float v, float uLength, float vLength, float z, vec4 color, vec4, uint32_t polyFlags)
{
	if (!locked || !frame)
		return;
	polyFlags = ApplyPrecedenceRules(polyFlags);
	vec4 blendColor = color;
	const bool subpixelFont = polyFlags == PF_SubpixelFont;
	if (subpixelFont)
		color = vec4(1.0f);
	WebGL2CachedTexture* texture = textures->GetTexture(&info, (polyFlags & PF_Masked) != 0);
	const float u0 = u * texture->UMult;
	const float v0 = v * texture->VMult;
	const float u1 = (u + uLength) * texture->UMult;
	const float v1 = (v + vLength) * texture->VMult;
	const bool clampTexture = u0 >= 0.0f && u1 <= 1.00001f && v0 >= 0.0f && v1 <= 1.00001f;
	const vec4 vertexColor = (polyFlags & PF_Modulated) ? vec4(1.0f) : vec4(color.x, color.y, color.z, 1.0f);
	const float x0 = x - frame->FX2;
	const float y0 = y - frame->FY2;
	const float x1 = x0 + width;
	const float y1 = y0 + height;
	const float sx = rfx2 * z;
	const float sy = rfy2 * z;
	std::vector<WebGL2SceneVertex> vertices(4);
	SetVertex(vertices[0], 0, vec3(sx * x0, sy * y0, z), vec2(u0, v0), vec2(0.0f), vec2(0.0f), vec2(0.0f), vertexColor);
	SetVertex(vertices[1], 0, vec3(sx * x1, sy * y0, z), vec2(u1, v0), vec2(0.0f), vec2(0.0f), vec2(0.0f), vertexColor);
	SetVertex(vertices[2], 0, vec3(sx * x1, sy * y1, z), vec2(u1, v1), vec2(0.0f), vec2(0.0f), vec2(0.0f), vertexColor);
	SetVertex(vertices[3], 0, vec3(sx * x0, sy * y1, z), vec2(u0, v1), vec2(0.0f), vec2(0.0f), vec2(0.0f), vertexColor);
	std::vector<uint32_t> indexes = { 0, 1, 2, 0, 2, 3 };
	DrawIndexed(GL_TRIANGLES, vertices, indexes, polyFlags, texture, nullptr, nullptr, nullptr, clampTexture,
		subpixelFont ? &blendColor : nullptr);
}

vec4 WebGL2RenderDevice::ApplyInverseGamma(vec4 color) const
{
	if (IsOrtho)
		return color;
	const float brightness = clamp(Brightness * 2.0f, 0.05f, 2.99f);
	return vec4(std::pow(color.r, std::max(brightness + GammaOffset + GammaOffsetRed, 0.001f)),
		std::pow(color.g, std::max(brightness + GammaOffset + GammaOffsetGreen, 0.001f)),
		std::pow(color.b, std::max(brightness + GammaOffset + GammaOffsetBlue, 0.001f)), color.a);
}

void WebGL2RenderDevice::Draw3DLine(FSceneNode* frame, vec4 color, uint32_t lineFlags, vec3 p1, vec3 p2)
{
	if (IsOrtho)
	{
		p1 = vec3(p1.x / frame->Zoom + frame->FX2, p1.y / frame->Zoom + frame->FY2, 1.0f);
		p2 = vec3(p2.x / frame->Zoom + frame->FX2, p2.y / frame->Zoom + frame->FY2, 1.0f);
		if (std::abs(p2.x - p1.x) + std::abs(p2.y - p1.y) >= 0.2f)
			Draw2DLine(frame, color, lineFlags, p1, p2);
		else if (IsOrthoLowDetail)
			Draw2DPoint(frame, color, LINE_None, p1.x - 1, p1.y - 1, p1.x + 1, p1.y + 1, p1.z);
		return;
	}
	const vec4 corrected = ApplyInverseGamma(vec4(color.x, color.y, color.z, 1.0f));
	std::vector<WebGL2SceneVertex> vertices(2);
	SetVertex(vertices[0], 0, p1, vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), corrected);
	SetVertex(vertices[1], 0, p2, vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), corrected);
	std::vector<uint32_t> indexes = { 0, 1 };
	const bool occlude = (lineFlags & LINE_DepthCued) != 0;
	DrawIndexed(GL_LINES, vertices, indexes, PF_Highlighted | (occlude ? PF_Occlude : 0), nullptr, nullptr, nullptr, nullptr, false, nullptr,
		occlude ? 0.1f : 0.0f, occlude ? 1.0f : 0.1f);
}

void WebGL2RenderDevice::Draw2DLine(FSceneNode* frame, vec4 color, uint32_t lineFlags, vec3 p1, vec3 p2)
{
	if (!frame)
		return;
	const vec4 corrected = ApplyInverseGamma(vec4(color.x, color.y, color.z, 1.0f));
	std::vector<WebGL2SceneVertex> vertices(2);
	SetVertex(vertices[0], 0, vec3(rfx2 * p1.z * (p1.x - frame->FX2), rfy2 * p1.z * (p1.y - frame->FY2), p1.z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), corrected);
	SetVertex(vertices[1], 0, vec3(rfx2 * p2.z * (p2.x - frame->FX2), rfy2 * p2.z * (p2.y - frame->FY2), p2.z), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), corrected);
	std::vector<uint32_t> indexes = { 0, 1 };
	const bool occlude = (lineFlags & LINE_DepthCued) != 0;
	DrawIndexed(GL_LINES, vertices, indexes, PF_Highlighted | (occlude ? PF_Occlude : 0), nullptr, nullptr, nullptr, nullptr, false, nullptr,
		occlude ? 0.1f : 0.0f, occlude ? 1.0f : 0.1f);
}

void WebGL2RenderDevice::Draw2DPoint(FSceneNode* frame, vec4 color, uint32_t lineFlags, float x1, float y1, float x2, float y2, float z)
{
	if (!frame)
		return;
	const vec4 corrected = ApplyInverseGamma(vec4(color.x, color.y, color.z, 1.0f));
	auto position = [&](float x, float y) { return vec3(rfx2 * z * (x - frame->FX2), rfy2 * z * (y - frame->FY2), z); };
	std::vector<WebGL2SceneVertex> vertices(4);
	SetVertex(vertices[0], 0, position(x1 - 0.5f, y1 - 0.5f), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), corrected);
	SetVertex(vertices[1], 0, position(x2 + 0.5f, y1 - 0.5f), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), corrected);
	SetVertex(vertices[2], 0, position(x2 + 0.5f, y2 + 0.5f), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), corrected);
	SetVertex(vertices[3], 0, position(x1 - 0.5f, y2 + 0.5f), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), corrected);
	std::vector<uint32_t> indexes = { 0, 1, 2, 0, 2, 3 };
	const bool occlude = (lineFlags & LINE_DepthCued) != 0;
	DrawIndexed(GL_TRIANGLES, vertices, indexes, PF_Highlighted | (occlude ? PF_Occlude : 0), nullptr, nullptr, nullptr, nullptr, false, nullptr,
		occlude ? 0.1f : 0.0f, occlude ? 1.0f : 0.1f);
}

void WebGL2RenderDevice::ClearZ()
{
	if (!EnsureReady())
		return;
	glDepthMask(GL_TRUE);
	glClearDepthf(1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);
	CountErrors();
}

void WebGL2RenderDevice::PushHit(const uint8_t*, int) { }
void WebGL2RenderDevice::PopHit(int, bool) { }

void WebGL2RenderDevice::ReadPixels(FColor* pixels)
{
	if (!pixels || !EnsureReady() || currentWidth <= 0 || currentHeight <= 0)
		return;
	std::vector<FColor> rows(static_cast<size_t>(currentWidth) * currentHeight);
	glReadPixels(0, 0, currentWidth, currentHeight, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
	for (int y = 0; y < currentHeight; y++)
	{
		const FColor* source = rows.data() + static_cast<size_t>(currentHeight - y - 1) * currentWidth;
		std::copy_n(source, currentWidth, pixels + static_cast<size_t>(y) * currentWidth);
	}
	CountErrors();
}

void WebGL2RenderDevice::EndFlash()
{
	if (!locked || (flashScale == vec4(0.5f, 0.5f, 0.5f, 0.0f) && flashFog == vec4(0.0f)))
		return;
	const mat4 savedMatrix = currentMatrix;
	UpdateSceneUniforms(mat4::identity());
	const vec4 color(flashFog.x, flashFog.y, flashFog.z, 1.0f - std::min(flashScale.x * 2.0f, 1.0f));
	std::vector<WebGL2SceneVertex> vertices(4);
	SetVertex(vertices[0], 0, vec3(-1.0f, -1.0f, 0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color);
	SetVertex(vertices[1], 0, vec3(1.0f, -1.0f, 0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color);
	SetVertex(vertices[2], 0, vec3(1.0f, 1.0f, 0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color);
	SetVertex(vertices[3], 0, vec3(-1.0f, 1.0f, 0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), vec2(0.0f), color);
	std::vector<uint32_t> indexes = { 0, 1, 2, 0, 2, 3 };
	DrawIndexed(GL_TRIANGLES, vertices, indexes, PF_Highlighted, nullptr, nullptr, nullptr, nullptr, false, nullptr, 0.0f, 0.1f);
	UpdateSceneUniforms(savedMatrix);
	if (currentFrame)
		SetSceneNode(currentFrame);
}

void WebGL2RenderDevice::SetSceneNode(FSceneNode* frame)
{
	if (!frame || !EnsureReady())
		return;
	currentFrame = frame;
	aspect = frame->FY / frame->FX;
	rProjZ = static_cast<float>(std::tan(radians(frame->FovAngle) * 0.5));
	rfx2 = 2.0f * rProjZ / frame->FX;
	rfy2 = 2.0f * rProjZ * aspect / frame->FY;
	const int x = std::max(frame->XB, 0);
	const int y = std::max(currentHeight - (frame->YB + frame->Y), 0);
	glViewport(x, y, std::max(frame->X, 0), std::max(frame->Y, 0));
	glDepthRangef(0.1f, 1.0f);
	UpdateSceneUniforms(frame->Projection * frame->WorldToView * frame->ObjectToWorld);
	CountErrors();
}

void WebGL2RenderDevice::PrecacheTexture(FTextureInfo& info, uint32_t polyFlags)
{
	if (!EnsureReady())
		return;
	polyFlags = ApplyPrecedenceRules(polyFlags);
	textures->GetTexture(&info, (polyFlags & PF_Masked) != 0);
}

bool WebGL2RenderDevice::SupportsTextureFormat(TextureFormat format)
{
	return textures && textures->SupportsTextureFormat(format);
}

void WebGL2RenderDevice::UpdateTextureRect(FTextureInfo& info, int u, int v, int width, int height)
{
	if (EnsureReady())
		textures->UpdateTextureRect(&info, u, v, width, height);
}

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextState() { return ActiveDevice && ActiveDevice->GetContext() ? static_cast<int>(ActiveDevice->GetContext()->State()) : -1; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextGeneration() { return ActiveDevice && ActiveDevice->GetContext() ? static_cast<int>(ActiveDevice->GetContext()->Generation()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextLossCount() { return ActiveDevice && ActiveDevice->GetContext() ? static_cast<int>(ActiveDevice->GetContext()->LossCount()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextHandle() { return ActiveDevice && ActiveDevice->GetContext() ? static_cast<int>(ActiveDevice->GetContext()->Handle()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2FrameCount() { return ActiveDevice ? static_cast<int>(ActiveDevice->FrameCount()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2SuppressedFrameCount() { return ActiveDevice ? static_cast<int>(ActiveDevice->SuppressedFrameCount()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2UnsupportedDrawCount() { return ActiveDevice ? static_cast<int>(ActiveDevice->UnsupportedDrawCount()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ErrorCount() { return ActiveDevice ? static_cast<int>(ActiveDevice->ErrorCount()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextLossStatusCount() { return ActiveDevice ? static_cast<int>(ActiveDevice->ContextLossStatusCount()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2DrawCallCount() { return ActiveDevice ? static_cast<int>(ActiveDevice->DrawCallCount()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2TextureCount() { return ActiveDevice ? static_cast<int>(ActiveDevice->TextureCount()) : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2DrawingBufferWidth() { return ActiveDevice ? ActiveDevice->DrawingBufferWidth() : 0; }
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2DrawingBufferHeight() { return ActiveDevice ? ActiveDevice->DrawingBufferHeight() : 0; }
}
