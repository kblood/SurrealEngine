#include "Precomp.h"
#include "WebGL2RenderDevice.h"

#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>
#include <surrealwidgets/core/widget.h>
#include <algorithm>
#include <vector>

namespace
{
	WebGL2RenderDevice* ActiveDevice = nullptr;
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
}

bool WebGL2RenderDevice::EnsureReady()
{
	if (!context || !context->IsReady() || !context->MakeCurrent())
		return false;
	if (initializedGeneration != context->Generation())
		InitializeGeneration();
	return initializedGeneration == context->Generation();
}

void WebGL2RenderDevice::InitializeGeneration()
{
	if (!context || !context->IsReady() || !context->MakeCurrent())
		return;
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glClearDepthf(1.0f);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	initializedGeneration = context->Generation();
	CountErrors();
}

void WebGL2RenderDevice::CountErrors()
{
	while (glGetError() != GL_NO_ERROR)
		errorCount++;
}

void WebGL2RenderDevice::CountUnsupportedDraw()
{
	unsupportedDrawCount++;
}

void WebGL2RenderDevice::Flush(bool)
{
	if (EnsureReady())
		glFlush();
}

void WebGL2RenderDevice::Lock(vec4, vec4, vec4 ScreenClear, uint8_t*, int* HitSize)
{
	if (HitSize)
		*HitSize = 0;
	locked = false;
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
	glViewport(0, 0, currentWidth, currentHeight);
	glDisable(GL_SCISSOR_TEST);
	glDepthMask(GL_TRUE);
	glClearColor(ScreenClear.x, ScreenClear.y, ScreenClear.z, ScreenClear.w);
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

void WebGL2RenderDevice::DrawComplexSurface(FSceneNode*, FSurfaceInfo&, FSurfaceFacet&)
{
	CountUnsupportedDraw();
}

void WebGL2RenderDevice::DrawGouraudPolygon(FSceneNode*, FTextureInfo&, const GouraudVertex*, int, uint32_t)
{
	CountUnsupportedDraw();
}

void WebGL2RenderDevice::DrawTile(FSceneNode*, FTextureInfo&, float, float, float, float, float, float, float, float, float, vec4, vec4, uint32_t)
{
	CountUnsupportedDraw();
}

void WebGL2RenderDevice::Draw3DLine(FSceneNode*, vec4, uint32_t, vec3, vec3)
{
	CountUnsupportedDraw();
}

void WebGL2RenderDevice::Draw2DLine(FSceneNode*, vec4, uint32_t, vec3, vec3)
{
	CountUnsupportedDraw();
}

void WebGL2RenderDevice::Draw2DPoint(FSceneNode*, vec4, uint32_t, float, float, float, float, float)
{
	CountUnsupportedDraw();
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

void WebGL2RenderDevice::PushHit(const uint8_t*, int)
{
}

void WebGL2RenderDevice::PopHit(int, bool)
{
}

void WebGL2RenderDevice::ReadPixels(FColor* pixels)
{
	if (!pixels || !EnsureReady() || currentWidth <= 0 || currentHeight <= 0)
		return;
	std::vector<FColor> rows(static_cast<size_t>(currentWidth) * static_cast<size_t>(currentHeight));
	glReadPixels(0, 0, currentWidth, currentHeight, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
	for (int y = 0; y < currentHeight; y++)
	{
		const auto* source = rows.data() + static_cast<size_t>(currentHeight - y - 1) * currentWidth;
		auto* destination = pixels + static_cast<size_t>(y) * currentWidth;
		std::copy_n(source, currentWidth, destination);
	}
	CountErrors();
}

void WebGL2RenderDevice::EndFlash()
{
}

void WebGL2RenderDevice::SetSceneNode(FSceneNode* frame)
{
	if (!frame || !EnsureReady())
		return;
	const int x = std::max(frame->XB, 0);
	const int y = std::max(currentHeight - (frame->YB + frame->Y), 0);
	const int width = std::max(frame->X, 0);
	const int height = std::max(frame->Y, 0);
	glViewport(x, y, width, height);
	CountErrors();
}

void WebGL2RenderDevice::PrecacheTexture(FTextureInfo&, uint32_t)
{
}

bool WebGL2RenderDevice::SupportsTextureFormat(TextureFormat)
{
	return false;
}

void WebGL2RenderDevice::UpdateTextureRect(FTextureInfo&, int, int, int, int)
{
}

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextState()
	{
		return ActiveDevice && ActiveDevice->GetContext() ? static_cast<int>(ActiveDevice->GetContext()->State()) : -1;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextGeneration()
	{
		return ActiveDevice && ActiveDevice->GetContext() ? static_cast<int>(ActiveDevice->GetContext()->Generation()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextLossCount()
	{
		return ActiveDevice && ActiveDevice->GetContext() ? static_cast<int>(ActiveDevice->GetContext()->LossCount()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ContextHandle()
	{
		return ActiveDevice && ActiveDevice->GetContext() ? static_cast<int>(ActiveDevice->GetContext()->Handle()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2FrameCount()
	{
		return ActiveDevice ? static_cast<int>(ActiveDevice->FrameCount()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2SuppressedFrameCount()
	{
		return ActiveDevice ? static_cast<int>(ActiveDevice->SuppressedFrameCount()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2UnsupportedDrawCount()
	{
		return ActiveDevice ? static_cast<int>(ActiveDevice->UnsupportedDrawCount()) : 0;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetWebGL2ErrorCount()
	{
		return ActiveDevice ? static_cast<int>(ActiveDevice->ErrorCount()) : 0;
	}
}
