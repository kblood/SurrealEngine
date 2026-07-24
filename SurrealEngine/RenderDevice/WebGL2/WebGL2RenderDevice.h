#pragma once

#include "RenderDevice/RenderDevice.h"
#include "WebGL2Context.h"
#include "WebGL2TextureManager.h"
#include "Math/mat.h"

#include <GLES3/gl3.h>
#include <cstdint>
#include <memory>
#include <vector>

struct WebGL2SceneVertex
{
	uint32_t Flags = 0;
	float Position[3] = {};
	float TexCoord[2] = {};
	float TexCoord2[2] = {};
	float TexCoord3[2] = {};
	float TexCoord4[2] = {};
	float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
};

class WebGL2RenderDevice : public RenderDevice
{
public:
	explicit WebGL2RenderDevice(Widget* viewport);
	~WebGL2RenderDevice() override;

	void Flush(bool AllowPrecache) override;
	void Lock(vec4 FlashScale, vec4 FlashFog, vec4 ScreenClear, uint8_t* HitData, int* HitSize) override;
	void Unlock(bool Blit) override;
	void DrawComplexSurface(FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet) override;
	void DrawGouraudPolygon(FSceneNode* Frame, FTextureInfo& Info, const GouraudVertex* Pts, int NumPts, uint32_t PolyFlags) override;
	void DrawTile(FSceneNode* Frame, FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags) override;
	void Draw3DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2) override;
	void Draw2DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2) override;
	void Draw2DPoint(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, float X1, float Y1, float X2, float Y2, float Z) override;
	void ClearZ() override;
	void PushHit(const uint8_t* Data, int Count) override;
	void PopHit(int Count, bool bForce) override;
	void ReadPixels(FColor* Pixels) override;
	void EndFlash() override;
	void SetSceneNode(FSceneNode* Frame) override;
	void PrecacheTexture(FTextureInfo& Info, uint32_t PolyFlags) override;
	bool SupportsTextureFormat(TextureFormat Format) override;
	void UpdateTextureRect(FTextureInfo& Info, int U, int V, int UL, int VL) override;

	WebGL2Context* GetContext() const { return context.get(); }
	uint32_t FrameCount() const { return frameCount; }
	uint32_t SuppressedFrameCount() const { return suppressedFrameCount; }
	uint32_t UnsupportedDrawCount() const { return unsupportedDrawCount; }
	uint32_t ErrorCount() const { return errorCount; }
	uint32_t ContextLossStatusCount() const { return contextLossStatusCount; }
	uint32_t DrawCallCount() const { return drawCallCount; }
	uint32_t SubmissionCount() const { return submissionCount; }
	uint32_t TextureCount() const { return textures ? static_cast<uint32_t>(textures->TextureCount()) : 0; }
	int DrawingBufferWidth() const { return currentWidth; }
	int DrawingBufferHeight() const { return currentHeight; }

private:
	struct ComplexSurfaceInfo
	{
		FSurfaceFacet* Facet = nullptr;
		WebGL2CachedTexture* Texture = nullptr;
		WebGL2CachedTexture* Lightmap = nullptr;
		WebGL2CachedTexture* MacroTexture = nullptr;
		WebGL2CachedTexture* DetailTexture = nullptr;
		WebGL2CachedTexture* FogMap = nullptr;
	};
	struct QueuedDraw
	{
		GLenum Mode = GL_TRIANGLES;
		size_t FirstIndex = 0;
		GLsizei IndexCount = 0;
		uint32_t PolyFlags = 0;
		WebGL2CachedTexture* Texture = nullptr;
		WebGL2CachedTexture* Lightmap = nullptr;
		WebGL2CachedTexture* MacroTexture = nullptr;
		WebGL2CachedTexture* DetailTexture = nullptr;
		bool ClampTexture = false;
		bool HasBlendColor = false;
		vec4 BlendColor = vec4(1.0f);
		float MinDepth = 0.1f;
		float MaxDepth = 1.0f;
	};

	bool EnsureReady();
	void InitializeGeneration();
	bool CreateResources();
	void ReleaseResources(bool deleteObjects);
	GLuint CompileShader(GLenum type, const char* source);
	void UpdateSceneUniforms(const mat4& matrix, bool webXRProjection = false);
	void ApplyPipelineState(uint32_t polyFlags, const vec4* blendColor = nullptr);
	void BindTextureUnit(int unit, WebGL2CachedTexture* texture, bool noSmooth, bool clamp);
	void SubmitQueuedDraws();
	void DrawIndexed(GLenum mode, const std::vector<WebGL2SceneVertex>& vertices, const std::vector<uint32_t>& indexes,
		uint32_t polyFlags, WebGL2CachedTexture* texture = nullptr, WebGL2CachedTexture* lightmap = nullptr,
		WebGL2CachedTexture* macroTexture = nullptr, WebGL2CachedTexture* detailTexture = nullptr,
		bool clampTexture = false, const vec4* blendColor = nullptr, float minDepth = 0.1f, float maxDepth = 1.0f);
	void DrawComplexSurfaceFaces(const ComplexSurfaceInfo& info, uint32_t polyFlags);
	vec4 ApplyInverseGamma(vec4 color) const;
	void CountErrors();
	void CountUnsupportedDraw();

	std::unique_ptr<WebGL2Context> context;
	std::unique_ptr<WebGL2TextureManager> textures;
	GLuint program = 0;
	GLuint vertexShader = 0;
	GLuint fragmentShader = 0;
	GLuint vertexArray = 0;
	GLuint vertexBuffer = 0;
	GLuint indexBuffer = 0;
	GLuint samplers[4] = {};
	GLint matrixUniform = -1;
	GLint alphaTestUniform = -1;
	GLint xrProjectionUniform = -1;
	uint32_t initializedGeneration = 0;
	uint32_t frameCount = 0;
	uint32_t suppressedFrameCount = 0;
	uint32_t unsupportedDrawCount = 0;
	uint32_t errorCount = 0;
	uint32_t contextLossStatusCount = 0;
	uint32_t drawCallCount = 0;
	uint32_t submissionCount = 0;
	int currentWidth = 0;
	int currentHeight = 0;
	FSceneNode* currentFrame = nullptr;
	float aspect = 0.0f;
	float rProjZ = 0.0f;
	float rfx2 = 0.0f;
	float rfy2 = 0.0f;
	vec4 flashScale = vec4(0.0f);
	vec4 flashFog = vec4(0.0f);
	mat4 currentMatrix = mat4::identity();
	bool resourcesReady = false;
	bool locked = false;
	std::vector<WebGL2SceneVertex> queuedVertices;
	std::vector<uint32_t> queuedIndexes;
	std::vector<QueuedDraw> queuedDraws;
};
