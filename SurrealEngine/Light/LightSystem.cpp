
#include "Precomp.h"
#include "LightSystem.h"
#include "Collision/TopLevel/CollisionHit.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Render/RenderSubsystem.h"
#include "Math/floating.h"
#include "Math/coords.h"
#include "Engine.h"

void LightSystem::UpdateLightList(UActor* actor)
{
	vec3 location = actor->BspInfo.BoundingBox.center();

	if (!actor->LightInfo.NeedsUpdate && actor->LightInfo.Location == location)
		return;

	actor->LightInfo.NeedsUpdate = false;
	actor->LightInfo.Location = location;
	actor->LightInfo.LightList.clear();

	if (actor->bUnlit())
		return;

	vec3 extents = actor->BspInfo.BoundingBox.extents();

	int checkCounter = NextCheckCounter();
	ivec3 start = GetStartExtents(location, extents);
	ivec3 end = GetEndExtents(location, extents);
	if (end.x - start.x < 100 && end.y - start.y < 100 && end.z - start.z < 100)
	{
		for (int z = start.z; z < end.z; z++)
		{
			for (int y = start.y; y < end.y; y++)
			{
				for (int x = start.x; x < end.x; x++)
				{
					for (UActor* light : GetActors(x, y, z))
					{
						if (light->Light.CheckCounter != checkCounter)
						{
							light->Light.CheckCounter = checkCounter;
							if (!light->bCorona() && !light->bSpecialLit())
							{
								float radius = light->WorldLightRadius();
								vec3 L = light->Location() - location;
								if (light->LightEffect() == LE_Cylinder) // Cylinder lights have infinite Z axis range
								{
									L.z = 0.0f;
								}
								if (dot(L, L) < radius * radius && !engine->Level->Collision.TraceAnyHit(light->Location(), location, actor, false, true, true))
								{
									actor->LightInfo.LightList.push_back(light);
								}
							}
						}
					}
				}
			}
		}
	}
}

float LightSystem::SampleLightLevel(const vec3& location, UActor* tracingActor)
{
	// How lit a spot looks is already decided by the lightmap the renderer paints
	// on the floor there, so read that instead of keeping a second lighting model
	// that would have to be kept in agreement with it.
	if (!engine->render || !engine->Level)
		return 0.0f;

	UModel* model = engine->Level->Model;
	const float probeDistance = 4096.0f;
	vec3 probeEnd = location - vec3(0.0f, 0.0f, probeDistance);

	for (const CollisionHit& hit : engine->Level->Collision.Trace(location, probeEnd, 0.0f, 0.0f, false, true, false))
	{
		if (hit.Actor || !hit.Node || hit.Node->Surf < 0)
			continue;

		BspSurface& surface = model->Surfaces[hit.Node->Surf];
		if (surface.LightMap < 0)
			continue;

		bool front = location.x * hit.Node->PlaneX + location.y * hit.Node->PlaneY + location.z * hit.Node->PlaneZ - hit.Node->PlaneW >= 0.0f;
		UZoneInfo* zoneActor = engine->GetZoneActor(front ? hit.Node->Zone1 : hit.Node->Zone0);
		if (!zoneActor)
			continue;

		FTextureInfo lightmap = engine->render->GetSurfaceLightmap(surface, zoneActor, model);
		if (!lightmap.Mips || lightmap.Format != TextureFormat::RGBA32_F)
			continue;

		vec3 point = location;
		point.z -= hit.Fraction;

		// Inverse of LightmapBuilder::CalcWorldLocations
		const LightMapIndex& lmindex = model->LightMap[surface.LightMap];
		const vec3& xaxis = model->Vectors[surface.vTextureU];
		const vec3& yaxis = model->Vectors[surface.vTextureV];
		const vec3& origin = model->Points[surface.pBase];
		float u = (dot(xaxis, point - origin) - lmindex.PanX) / lmindex.UScale + 0.5f;
		float v = (dot(yaxis, point - origin) - lmindex.PanY) / lmindex.VScale + 0.5f;

		// Texel (x,y) holds the light at u = x, v = y + 0.5
		float fx = u - std::floor(u);
		float fy = v - 0.5f - std::floor(v - 0.5f);
		int x0 = std::clamp((int)std::floor(u), 0, lightmap.USize - 1);
		int y0 = std::clamp((int)std::floor(v - 0.5f), 0, lightmap.VSize - 1);
		int x1 = std::min(x0 + 1, lightmap.USize - 1);
		int y1 = std::min(y0 + 1, lightmap.VSize - 1);

		const vec4* texels = (const vec4*)lightmap.Mips[0].Data.data();
		auto luminance = [&](int x, int y)
		{
			const vec4& texel = texels[(size_t)y * lightmap.USize + x];
			return (texel.r + texel.g + texel.b) * (1.0f / 3.0f);
		};

		float level = mix(
			mix(luminance(x0, y0), luminance(x1, y0), fx),
			mix(luminance(x0, y1), luminance(x1, y1), fx), fy);
		return std::clamp(level, 0.0f, 1.0f);
	}

	return 0.0f;
}

void LightSystem::SetLevel(ULevel* level)
{
	Level = level;
}

void LightSystem::AddLight(UActor* light)
{
	if (light->LightType() != LT_None && light->LightBrightness() > 0)
	{
		vec3 location = light->Location();
		float radius = light->WorldLightRadius();

		light->Light.Inserted = true;
		light->Light.Location = location;
		light->Light.Radius = radius;

		ivec3 start = GetStartExtents(location, radius);
		ivec3 end = GetEndExtents(location, radius);
		for (int z = start.z; z < end.z; z++)
		{
			for (int y = start.y; y < end.y; y++)
			{
				for (int x = start.x; x < end.x; x++)
				{
					LightActors[GetBucketId(x, y, z)].push_back(light);
				}
			}
		}
	}
}

void LightSystem::RemoveLight(UActor* light)
{
	if (light->Light.Inserted)
	{
		vec3 location = light->Light.Location;
		float radius = light->Light.Radius;

		ivec3 start = GetStartExtents(location, radius);
		ivec3 end = GetEndExtents(location, radius);
		for (int z = start.z; z < end.z; z++)
		{
			for (int y = start.y; y < end.y; y++)
			{
				for (int x = start.x; x < end.x; x++)
				{
					auto it = LightActors.find(GetBucketId(x, y, z));
					if (it != LightActors.end())
					{
						it->second.remove(light);
						if (it->second.empty())
							LightActors.erase(it);
					}
				}
			}
		}

		light->Light.Inserted = false;
	}
}
