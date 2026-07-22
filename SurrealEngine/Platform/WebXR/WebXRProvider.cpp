#include "Platform/WebXR/WebXRFrameBridge.h"

#include "Engine.h"
#include "Render/RenderSubsystem.h"

#include <cmath>

namespace
{
	constexpr float DefaultWorldUnitsPerMeter = 1.0f / 0.0254f;
	float WorldUnitsPerMeter = DefaultWorldUnitsPerMeter;
	WebXR::RecenterState Recenter;
}

#ifdef __EMSCRIPTEN__
#include "RenderDevice/WebGPU/WebGPURenderDevice.h"
#include <emscripten.h>

EM_JS(uintptr_t, JS_ImportCurrentWebXRTexture, (uintptr_t parentDevice), {
	const texture = globalThis.surrealWebXRFrameTexture;
	if (!texture || typeof WebGPU === "undefined" || !WebGPU.importJsTexture)
		return 0;
	return WebGPU.importJsTexture(texture, parentDevice);
});

namespace
{
	void ReleaseFrameResources(WebGPURenderDevice* device, const PresentationTarget& target,
		WGPUTexture texture, WGPUTextureView* views, uint32_t viewCount, bool bound)
	{
		if (bound)
			device->UnbindPresentationTarget(target);
		for (uint32_t index = 0; index < viewCount; index++)
		{
			if (views[index])
				wgpuTextureViewRelease(views[index]);
		}
		if (texture)
			wgpuTextureRelease(texture);
	}
}
#endif

extern "C"
{
	float Surreal_GetWebXRWorldUnitsPerMeter() { return WorldUnitsPerMeter; }

	int Surreal_SetWebXRWorldUnitsPerMeter(float value)
	{
		if (!std::isfinite(value) || value < 1.0f || value > 10000.0f)
			return 0;
		WorldUnitsPerMeter = value;
		return 1;
	}

	void Surreal_ResetWebXRPose() { Recenter.Valid = false; }
	uint32_t Surreal_GetWebXRPoseRecenterCount() { return Recenter.RecenterCount; }

	int Surreal_RenderWebXRFrame(const void* frameData, uint32_t bufferBytes)
	{
		WebXR::DecodedFrame frame;
		WebXR::FrameError error;
		if (!WebXR::DecodeFrame(frameData, bufferBytes, frame, error))
		{
			WebXR::SetLastFrameError(error);
			return 0;
		}

#ifdef __EMSCRIPTEN__
		if (!engine || !engine->render)
		{
			WebXR::SetLastFrameError(WebXR::FrameError::RenderDeviceUnavailable);
			return 0;
		}

		auto* device = dynamic_cast<WebGPURenderDevice*>(engine->render->Device);
		if (!device || !device->Context || !device->Context->Device)
		{
			WebXR::SetLastFrameError(WebXR::FrameError::RenderDeviceUnavailable);
			return 0;
		}

		WGPUTexture texture = reinterpret_cast<WGPUTexture>(
			JS_ImportCurrentWebXRTexture(reinterpret_cast<uintptr_t>(device->Context->Device)));
		if (!texture)
		{
			WebXR::SetLastFrameError(WebXR::FrameError::TextureUnavailable);
			return 0;
		}
		const WGPUTextureFormat textureFormat = wgpuTextureGetFormat(texture);

		WGPUTextureView views[WebXR::MaxViews] = {};
		WebGPUPresentationImageHandle handles[WebXR::MaxViews] = {};
		PresentationTargetBinding binding;
		binding.Target = { 1 };
		for (uint32_t index = 0; index < frame.Header.ViewCount; index++)
		{
			WGPUTextureViewDescriptor description = {};
			description.format = textureFormat;
			description.dimension = WGPUTextureViewDimension_2D;
			description.baseMipLevel = 0;
			description.mipLevelCount = 1;
			description.baseArrayLayer = frame.Views[index].ArrayLayer;
			description.arrayLayerCount = 1;
			description.aspect = WGPUTextureAspect_All;
			views[index] = wgpuTextureCreateView(texture, &description);
			if (!views[index])
			{
				ReleaseFrameResources(device, binding.Target, texture, views,
					frame.Header.ViewCount, false);
				WebXR::SetLastFrameError(WebXR::FrameError::PresentationRejected);
				return 0;
			}
			handles[index] = { views[index], textureFormat };
			binding.Images.push_back({ &handles[index],
				static_cast<int>(frame.Header.TextureWidth), static_cast<int>(frame.Header.TextureHeight) });
		}

		if (!device->BindPresentationTarget(binding))
		{
			ReleaseFrameResources(device, binding.Target, texture, views, frame.Header.ViewCount, false);
			WebXR::SetLastFrameError(WebXR::FrameError::PresentationRejected);
			return 0;
		}

		engine->tickCount++;
		const float levelElapsed = engine->AdvanceGameFrame();
		const Coords bodyRotation = Coords::Rotation(Rotator(0, engine->CameraRotation.Yaw, 0));
		ViewFamily family = WebXR::BuildViewFamily(frame, engine->CameraLocation,
			bodyRotation, WorldUnitsPerMeter, Recenter);
		engine->RenderGameFrame(levelElapsed, family);
		engine->FinishGameFrame(levelElapsed);

		ReleaseFrameResources(device, binding.Target, texture, views, frame.Header.ViewCount, true);
		WebXR::SetLastFrameError(WebXR::FrameError::None);
		return 1;
#else
		WebXR::SetLastFrameError(WebXR::FrameError::RenderDeviceUnavailable);
		return 0;
#endif
	}
}
