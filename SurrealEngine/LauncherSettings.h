#pragma once

#include "RenderDevice/RenderDeviceSelection.h"
#include "XR/XRCommon.h"

enum class AntialiasMode
{
	Off,
	MSAA2x,
	MSAA4x
};

enum class LightMode
{
	Normal,
	OneX,
	BrighterActors
};

enum class GammaMode
{
	D3D9,
	XOpenGL
};

struct XRLauncherSettings
{
	bool Enabled = false;
	XRHand DominantHand = XRHand::Right;
	bool FullBodyAvatar = false;
};

class LauncherSettings
{
public:
	static LauncherSettings& Get();
	void Save();

	struct
	{
#ifdef __EMSCRIPTEN__
		RenderDeviceType Type = RenderDeviceType::Null;
#else
		RenderDeviceType Type = RenderDeviceType::Vulkan;
#endif
		bool UseVSync = true;
		AntialiasMode Antialias = AntialiasMode::MSAA4x;
		LightMode Light = LightMode::Normal;
		GammaMode Gamma = GammaMode::D3D9;
		bool GammaCorrectScreenshots = false;
		bool Hdr = false;
		int HdrScale = 128;
		bool Bloom = false;
		int BloomAmount = 128;
		bool UseDebugLayer = false;
	} RenderDevice;

	struct
	{
		Array<std::string> SearchList;
		int LastSelected = -1;
	} Games;

	XRLauncherSettings XR;

private:
	LauncherSettings();
};
