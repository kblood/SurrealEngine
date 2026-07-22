#include "Platform/WebXR/WebXRFrameBridge.h"
#include "Platform/WebXR/WebXRInputAdapter.h"
#include "Platform/WebXR/WebXRInputRuntime.h"
#include "Platform/WebXR/WebXRUIProvider.h"

#include "Engine.h"
#include "Render/RenderSubsystem.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace
{
	constexpr float DefaultWorldUnitsPerMeter = 1.0f / 0.0254f;
	float WorldUnitsPerMeter = DefaultWorldUnitsPerMeter;
	WebXR::RecenterState Recenter;
	WebXR::InputRuntime InputRuntime;
	WebXR::UIInputConnector UIInput;

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

	WebXR::PackedPointerFeedback PackPointerFeedback(const WebXR::PointerFeedback& source)
	{
		WebXR::PackedPointerFeedback result;
		result.Active = source.Active ? 1u : 0u;
		result.Hit = source.Contact.Hit ? 1u : 0u;
		result.Surface = static_cast<uint32_t>(source.Contact.Surface);
		result.Hand = source.Hand == XRHand::Left ? 0u : 1u;
		for (size_t index = 0; index < 3; index++)
		{
			result.RayOrigin[index] = source.Ray.Origin[static_cast<int>(index)];
			result.RayDirection[index] = source.Ray.Direction[static_cast<int>(index)];
			result.HitPoint[index] = source.HitPoint[static_cast<int>(index)];
		}
		result.UV[0] = source.Contact.UV.x;
		result.UV[1] = source.Contact.UV.y;
		result.Pixel[0] = source.Contact.Pixel.x;
		result.Pixel[1] = source.Contact.Pixel.y;
		result.Distance = source.Contact.Distance;
		return result;
	}
}

#ifdef __EMSCRIPTEN__
#include "RenderDevice/WebGPU/WebGPURenderDevice.h"
#include "Platform/WebXR/WebXRUICompositor.h"
#include <emscripten.h>

EM_JS(uintptr_t, JS_ImportCurrentWebXRTexture, (uintptr_t parentDevice, uint32_t textureIndex), {
	const textures = globalThis.surrealWebXRFrameTextures;
	const texture = textures && textures[textureIndex];
	if (!texture || typeof WebGPU === "undefined" || !WebGPU.importJsTexture)
		return 0;
	return WebGPU.importJsTexture(texture, parentDevice);
});

namespace
{
	void ReleaseFrameResources(WebGPURenderDevice* device, const PresentationTarget& target,
		WGPUTexture* textures, uint32_t textureCount, WGPUTextureView* views,
		uint32_t viewCount, bool bound)
	{
		if (bound)
			device->UnbindPresentationTarget(target);
		for (uint32_t index = 0; index < viewCount; index++)
		{
			if (views[index])
				wgpuTextureViewRelease(views[index]);
		}
		for (uint32_t index = 0; index < textureCount; index++)
		{
			if (textures[index])
				wgpuTextureRelease(textures[index]);
		}
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

	void Surreal_ResetWebXRPose()
	{
		Recenter.Valid = false;
		if (engine && engine->render)
		{
			UIInput.Cancel(engine->render->XRUISurfaces());
			engine->render->XRUISurfaces().ClearViewerPose();
		}
	}
	uint32_t Surreal_GetWebXRPoseRecenterCount() { return Recenter.RecenterCount; }
	uint32_t Surreal_GetWebXRPointerFeedbackSize() { return sizeof(WebXR::PackedPointerFeedback); }

	int Surreal_GetWebXRPointerFeedback(uint32_t hand, void* output, uint32_t outputBytes)
	{
		if (!output || outputBytes != sizeof(WebXR::PackedPointerFeedback) || hand >= XRHandCount)
			return 0;
		const WebXR::PackedPointerFeedback packed = PackPointerFeedback(UIInput.Feedback()[hand]);
		std::memcpy(output, &packed, sizeof(packed));
		return 1;
	}

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

		WGPUTexture textures[WebXR::MaxViews] = {};
		for (uint32_t index = 0; index < frame.Header.TextureCount; index++)
		{
			textures[index] = reinterpret_cast<WGPUTexture>(JS_ImportCurrentWebXRTexture(
				reinterpret_cast<uintptr_t>(device->Context->Device), index));
			if (!textures[index])
			{
				WGPUTextureView emptyViews[WebXR::MaxViews] = {};
				ReleaseFrameResources(device, { 1 }, textures, frame.Header.TextureCount,
					emptyViews, 0, false);
				WebXR::SetLastFrameError(WebXR::FrameError::TextureUnavailable);
				return 0;
			}
		}
		WGPUTextureView views[WebXR::MaxViews] = {};
		WebGPUPresentationImageHandle handles[WebXR::MaxViews] = {};
		PresentationTargetBinding binding;
		binding.Target = { 1 };
		for (uint32_t index = 0; index < frame.Header.ViewCount; index++)
		{
			const WebXR::PackedView& source = frame.Views[index];
			WGPUTexture texture = textures[source.TextureIndex];
			if (wgpuTextureGetWidth(texture) != source.TextureWidth ||
				wgpuTextureGetHeight(texture) != source.TextureHeight ||
				source.ArrayLayer >= wgpuTextureGetDepthOrArrayLayers(texture))
			{
				ReleaseFrameResources(device, binding.Target, textures, frame.Header.TextureCount,
					views, frame.Header.ViewCount, false);
				WebXR::SetLastFrameError(WebXR::FrameError::PresentationRejected);
				return 0;
			}
			const WGPUTextureFormat textureFormat = wgpuTextureGetFormat(texture);
			if (index > 0 && textureFormat != handles[0].Format)
			{
				ReleaseFrameResources(device, binding.Target, textures, frame.Header.TextureCount,
					views, frame.Header.ViewCount, false);
				WebXR::SetLastFrameError(WebXR::FrameError::PresentationRejected);
				return 0;
			}
			WGPUTextureViewDescriptor description = {};
			description.format = textureFormat;
			description.dimension = WGPUTextureViewDimension_2D;
			description.baseMipLevel = 0;
			description.mipLevelCount = 1;
			description.baseArrayLayer = source.ArrayLayer;
			description.arrayLayerCount = 1;
			description.aspect = WGPUTextureAspect_All;
			views[index] = wgpuTextureCreateView(texture, &description);
			if (!views[index])
			{
				ReleaseFrameResources(device, binding.Target, textures, frame.Header.TextureCount,
					views, frame.Header.ViewCount, false);
				WebXR::SetLastFrameError(WebXR::FrameError::PresentationRejected);
				return 0;
			}
			handles[index] = { views[index], textureFormat };
			binding.Images.push_back({ &handles[index], static_cast<int>(source.TextureWidth),
				static_cast<int>(source.TextureHeight) });
		}

		if (!device->BindPresentationTarget(binding))
		{
			ReleaseFrameResources(device, binding.Target, textures, frame.Header.TextureCount,
				views, frame.Header.ViewCount, false);
			WebXR::SetLastFrameError(WebXR::FrameError::PresentationRejected);
			return 0;
		}
		if (!WebXR::BindUISurfaceTargets(device, handles[0].Format))
		{
			ReleaseFrameResources(device, binding.Target, textures, frame.Header.TextureCount,
				views, frame.Header.ViewCount, true);
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
			XRUISurfaceEngineBinding& ui = engine->render->XRUISurfaces();
			for (const XRUICanvasCaptureDescriptor& descriptor :
				WebXR::BuildUICaptureDescriptors(WorldUnitsPerMeter))
				ui.Configure(descriptor);
			ui.SetViewerPose(WebXR::BuildUIViewerPose(family));
			UIInput.Update(WebXR::AdaptInputSnapshot(WebXR::GetInputSnapshot()), ui,
				engine->CameraLocation, bodyRotation, WorldUnitsPerMeter, Recenter);
			engine->RenderGameFrame(levelElapsed, family);
			const XRUICanvasReplayFrame replayFrame = ui.BuildReplayFrame();
			if (!WebXR::CompositeUISurfaces(device, handles[0].Format, family, replayFrame,
				views, frame.Header.ViewCount))
				throw std::runtime_error("WebXR UI composition failed");
			engine->FinishGameFrame(levelElapsed);
		}
		catch (...)
		{
			WebXR::UnbindUISurfaceTargets(device);
			ReleaseFrameResources(device, binding.Target, textures, frame.Header.TextureCount,
				views, frame.Header.ViewCount, true);
			WebXR::SetLastFrameError(WebXR::FrameError::RenderFailed);
			return 0;
		}

		WebXR::UnbindUISurfaceTargets(device);
		ReleaseFrameResources(device, binding.Target, textures, frame.Header.TextureCount,
			views, frame.Header.ViewCount, true);
		WebXR::SetLastFrameError(WebXR::FrameError::None);
		return 1;
#else
		WebXR::SetLastFrameError(WebXR::FrameError::RenderDeviceUnavailable);
		return 0;
#endif
	}
}
