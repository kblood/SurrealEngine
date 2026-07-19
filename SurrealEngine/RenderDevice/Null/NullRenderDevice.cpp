
#include "Precomp.h"
#include "NullRenderDevice.h"
#include <surrealwidgets/core/widget.h>
#include <cstring>

NullRenderDevice::NullRenderDevice(Widget* viewport)
{
	Viewport = viewport;
}

void NullRenderDevice::Flush(bool AllowPrecache)
{
}

void NullRenderDevice::Lock(vec4 FlashScale, vec4 FlashFog, vec4 ScreenClear, uint8_t* HitData, int* HitSize)
{
	if (HitSize)
		*HitSize = 0;
}

void NullRenderDevice::Unlock(bool Blit)
{
}

void NullRenderDevice::DrawComplexSurface(FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet)
{
}

void NullRenderDevice::DrawGouraudPolygon(FSceneNode* Frame, FTextureInfo& Info, const GouraudVertex* Pts, int NumPts, uint32_t PolyFlags)
{
}

void NullRenderDevice::DrawTile(FSceneNode* Frame, FTextureInfo& Info, float X, float Y, float XL, float YL, float U, float V, float UL, float VL, float Z, vec4 Color, vec4 Fog, uint32_t PolyFlags)
{
}

void NullRenderDevice::Draw3DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
}

void NullRenderDevice::Draw2DLine(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, vec3 P1, vec3 P2)
{
}

void NullRenderDevice::Draw2DPoint(FSceneNode* Frame, vec4 Color, uint32_t LineFlags, float X1, float Y1, float X2, float Y2, float Z)
{
}

void NullRenderDevice::ClearZ()
{
}

void NullRenderDevice::PushHit(const uint8_t* Data, int Count)
{
}

void NullRenderDevice::PopHit(int Count, bool bForce)
{
}

void NullRenderDevice::ReadPixels(FColor* Pixels)
{
	if (Pixels)
		std::memset(Pixels, 0, (size_t)GetRenderWidth() * (size_t)GetRenderHeight() * sizeof(FColor));
}

void NullRenderDevice::EndFlash()
{
}

void NullRenderDevice::SetSceneNode(FSceneNode* Frame)
{
}

void NullRenderDevice::PrecacheTexture(FTextureInfo& Info, uint32_t PolyFlags)
{
}

bool NullRenderDevice::SupportsTextureFormat(TextureFormat Format)
{
	return false;
}

void NullRenderDevice::UpdateTextureRect(FTextureInfo& Info, int U, int V, int UL, int VL)
{
}
