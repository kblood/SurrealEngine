#include "Precomp.h"
#include "WebXRFrameBridge.h"

#include "Engine.h"
#include "Render/RenderSubsystem.h"

#include <array>
#include <cmath>
#include <cstring>

namespace
{
	enum FrameError
	{
		FrameOK = 0,
		FrameInvalidHeader = 1,
		FrameInvalidView = 2,
		FrameTextureUnavailable = 3,
		FrameRenderDeviceUnavailable = 4,
		FrameExternalTargetRejected = 5,
		FrameRenderFailed = 6
	};

	int LastFrameError = FrameOK;

	bool IsFiniteArray(const float* values, size_t count)
	{
		for (size_t i = 0; i < count; i++)
		{
			if (!std::isfinite(values[i]))
				return false;
		}
		return true;
	}

	bool DecodeFrame(const void* frameData, uint32_t bufferBytes, WebXRFrameABI::FrameHeader& header,
		std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews>& views)
	{
		if (!frameData || bufferBytes < sizeof(WebXRFrameABI::FrameHeader))
		{
			LastFrameError = FrameInvalidHeader;
			return false;
		}

		std::memcpy(&header, frameData, sizeof(header));
		if (header.version != WebXRFrameABI::Version || header.flags != WebXRFrameABI::KnownFlags ||
			header.viewCount == 0 || header.viewCount > WebXRFrameABI::MaxViews ||
			header.byteSize != bufferBytes ||
			header.byteSize != sizeof(header) + header.viewCount * sizeof(WebXRFrameABI::View) ||
			header.textureWidth == 0 || header.textureHeight == 0 || !std::isfinite(header.timestamp))
		{
			LastFrameError = FrameInvalidHeader;
			return false;
		}

		const uint8_t* viewBytes = static_cast<const uint8_t*>(frameData) + sizeof(header);
		for (uint32_t index = 0; index < header.viewCount; index++)
		{
			WebXRFrameABI::View& view = views[index];
			std::memcpy(&view, viewBytes + index * sizeof(view), sizeof(view));

			if (view.eye > WebXRFrameABI::Right || view.viewportX < 0 || view.viewportY < 0 ||
				view.viewportWidth <= 0 || view.viewportHeight <= 0 ||
				static_cast<uint32_t>(view.viewportWidth) > header.textureWidth ||
				static_cast<uint32_t>(view.viewportHeight) > header.textureHeight ||
				static_cast<uint32_t>(view.viewportX) > header.textureWidth - static_cast<uint32_t>(view.viewportWidth) ||
				static_cast<uint32_t>(view.viewportY) > header.textureHeight - static_cast<uint32_t>(view.viewportHeight) ||
				!IsFiniteArray(view.position, 3) || !IsFiniteArray(view.orientation, 4) ||
				!IsFiniteArray(view.projection, 16))
			{
				LastFrameError = FrameInvalidView;
				return false;
			}
		}

		LastFrameError = FrameOK;
		return true;
	}

	void BuildSceneViews(const WebXRFrameABI::FrameHeader& header,
		const std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews>& packedViews,
		std::array<WebXRSceneView, WebXRFrameABI::MaxViews>& sceneViews)
	{
		const Coords bodyRotation = Coords::Rotation(engine->CameraRotation);
		const mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() *
			bodyRotation.Inverse().ToMatrix() * Coords::Location(engine->CameraLocation).ToMatrix();

		for (uint32_t index = 0; index < header.viewCount; index++)
		{
			const WebXRFrameABI::View& source = packedViews[index];
			WebXRSceneView& target = sceneViews[index];
			target.Eye = source.eye;
			target.ArrayLayer = source.arrayLayer;
			target.ViewportX = source.viewportX;
			target.ViewportY = source.viewportY;
			target.ViewportWidth = source.viewportWidth;
			target.ViewportHeight = source.viewportHeight;
			target.Location = engine->CameraLocation;
			target.WorldToView = worldToView;
			target.ViewRotation = bodyRotation;
			target.Projection = mat4::from_values(const_cast<float*>(source.projection));
		}
	}
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "RenderDevice/WebGPU/WebGPURenderDevice.h"

EM_JS(uintptr_t, JS_ImportCurrentWebXRFrameTexture, (uintptr_t parentDevice), {
	const texture = window.surrealWebXRFrameTexture;
	if (!texture || typeof WebGPU === "undefined" || !WebGPU.importJsTexture)
		return 0;
	return WebGPU.importJsTexture(texture, parentDevice);
});
#endif

extern "C"
{
	uint32_t Surreal_GetWebXRFrameABIVersion() { return WebXRFrameABI::Version; }
	uint32_t Surreal_GetWebXRFrameHeaderSize() { return sizeof(WebXRFrameABI::FrameHeader); }
	uint32_t Surreal_GetWebXRViewSize() { return sizeof(WebXRFrameABI::View); }
	uint32_t Surreal_GetWebXRFrameMaxViews() { return WebXRFrameABI::MaxViews; }

	int Surreal_ValidateWebXRFrame(const void* frameData, uint32_t bufferBytes)
	{
		WebXRFrameABI::FrameHeader header = {};
		std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews> views = {};
		return DecodeFrame(frameData, bufferBytes, header, views) ? 1 : 0;
	}

	int Surreal_GetWebXRFrameLastError() { return LastFrameError; }

	int Surreal_RenderWebXRFrame(const void* frameData, uint32_t bufferBytes)
	{
		WebXRFrameABI::FrameHeader header = {};
		std::array<WebXRFrameABI::View, WebXRFrameABI::MaxViews> packedViews = {};
		if (!DecodeFrame(frameData, bufferBytes, header, packedViews))
			return 0;

#ifdef __EMSCRIPTEN__
		if (!engine || !engine->render)
		{
			LastFrameError = FrameRenderDeviceUnavailable;
			return 0;
		}

		auto* device = dynamic_cast<WebGPURenderDevice*>(engine->render->Device);
		if (!device || !device->Context || !device->Context->Device)
		{
			LastFrameError = FrameRenderDeviceUnavailable;
			return 0;
		}

		WGPUTexture texture = reinterpret_cast<WGPUTexture>(
			JS_ImportCurrentWebXRFrameTexture(reinterpret_cast<uintptr_t>(device->Context->Device)));
		if (!texture)
		{
			LastFrameError = FrameTextureUnavailable;
			return 0;
		}

		const WebXRFrameABI::View& firstView = packedViews[0];
		if (!device->BeginExternalRenderTargetFrame(texture,
			static_cast<int>(header.textureWidth), static_cast<int>(header.textureHeight), firstView.arrayLayer))
		{
			wgpuTextureRelease(texture);
			LastFrameError = FrameExternalTargetRejected;
			return 0;
		}
		if (!device->SelectExternalRenderTargetView(firstView.arrayLayer, firstView.viewportX,
			firstView.viewportY, firstView.viewportWidth, firstView.viewportHeight))
		{
			device->EndExternalRenderTargetFrame();
			LastFrameError = FrameExternalTargetRejected;
			return 0;
		}

		std::array<WebXRSceneView, WebXRFrameABI::MaxViews> sceneViews = {};
		BuildSceneViews(header, packedViews, sceneViews);
		const uint64_t tickBefore = engine->tickCount;
		const float levelElapsed = engine->AdvanceGameFrame();
		const bool rendered = engine->render->DrawGameWebXRViews(
			levelElapsed, sceneViews.data(), header.viewCount);
		engine->FinishGameFrame(levelElapsed);
		const bool ended = device->EndExternalRenderTargetFrame();
		if (!rendered || !ended || engine->tickCount != tickBefore + 1)
		{
			LastFrameError = FrameRenderFailed;
			return 0;
		}

		LastFrameError = FrameOK;
		return 1;
#else
		LastFrameError = FrameRenderDeviceUnavailable;
		return 0;
#endif
	}
}
