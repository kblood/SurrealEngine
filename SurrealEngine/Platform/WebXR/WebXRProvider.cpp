#include "Platform/WebXR/WebXRFrameBridge.h"
#include "Platform/WebXR/WebXRInputAdapter.h"
#include "Platform/WebXR/WebXRInputRuntime.h"

#include "Engine.h"
#include "Render/RenderSubsystem.h"

#include <cmath>

namespace
{
	constexpr float DefaultWorldUnitsPerMeter = 1.0f / 0.0254f;
	float WorldUnitsPerMeter = DefaultWorldUnitsPerMeter;
	WebXR::RecenterState Recenter;
	WebXR::InputRuntime InputRuntime;

	class EngineInputTarget final : public WebXR::RuntimeInputTarget
	{
	public:
		explicit EngineInputTarget(Engine* instance) : instance(instance) {}

		void SetButton(InputSourceId source, int32_t control, bool pressed) override
		{
			instance->InputEvent(static_cast<EInputKey>(control),
				pressed ? EInputType::IST_Press : EInputType::IST_Release, 0.0f, source);
		}

		void SetAxis(InputSourceId source, int32_t control, float value) override
		{
			instance->InputEvent(static_cast<EInputKey>(control), EInputType::IST_Axis, value, source);
		}

		void ReleaseSource(InputSourceId source) override
		{
			instance->ReleaseInputSource(source);
		}

	private:
		Engine* instance;
	};

	WebXR::RuntimeInputBindings EngineBindings()
	{
		WebXR::RuntimeInputBindings bindings;
		bindings.Hands[static_cast<size_t>(XRHand::Left)] = {
			{ IK_Joy1, IK_Joy2, IK_Joy3, IK_Joy4, IK_Joy5, IK_Joy6 }, IK_JoyX, IK_JoyY
		};
		bindings.Hands[static_cast<size_t>(XRHand::Right)] = {
			{ IK_Joy9, IK_Joy10, IK_Joy11, IK_Joy12, IK_Joy13, IK_Joy14 }, IK_JoyU, IK_JoyV
		};
		return bindings;
	}
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
	int Surreal_ApplyWebXRInputSnapshot()
	{
		if (!engine)
			return 0;
		EngineInputTarget target(engine);
		InputRuntime.Apply(WebXR::AdaptInputSnapshot(WebXR::GetInputSnapshot()), EngineBindings(), target);
		return 1;
	}

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

		try
		{
			engine->tickCount++;
			const float levelElapsed = engine->AdvanceGameFrame();
			const Coords bodyRotation = Coords::Rotation(Rotator(0, engine->CameraRotation.Yaw, 0));
			ViewFamily family = WebXR::BuildViewFamily(frame, engine->CameraLocation,
				bodyRotation, WorldUnitsPerMeter, Recenter);
			engine->RenderGameFrame(levelElapsed, family);
			engine->FinishGameFrame(levelElapsed);
		}
		catch (...)
		{
			ReleaseFrameResources(device, binding.Target, texture, views, frame.Header.ViewCount, true);
			WebXR::SetLastFrameError(WebXR::FrameError::RenderFailed);
			return 0;
		}

		ReleaseFrameResources(device, binding.Target, texture, views, frame.Header.ViewCount, true);
		WebXR::SetLastFrameError(WebXR::FrameError::None);
		return 1;
#else
		WebXR::SetLastFrameError(WebXR::FrameError::RenderDeviceUnavailable);
		return 0;
#endif
	}
}
