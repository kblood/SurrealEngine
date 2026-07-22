#pragma once

#include "Engine.h"
#include "VisibleNode.h"
#include "VisibleTranslucent.h"
#include "VisibleCorona.h"
#include "VisibleActor.h"
#include "VisiblePortal.h"
#include "BspClipper.h"
#include "RenderDevice/RenderDevice.h"
#include "UObject/UActor.h"
#include "UObject/UTexture.h"
#include "UObject/UFont.h"
#include "UObject/ULevel.h"
#include "UObject/UClient.h"

// Explicit viewport rect override for VisibleFrame::Process, used by the
// --debugstereo diagnostic (and, later, real per-eye VR rendering) to
// render into a sub-rect of the window instead of the full viewport
// SetupSceneFrame would otherwise derive from engine->viewport. Optionally
// also carries an explicit (possibly asymmetric) projection matrix - when
// set, this is used verbatim instead of the symmetric FovAngle-derived
// frustum, both for the CPU-side BSP clipper and (via
// FSceneNode::ProjectionOverride) the render device. See
// VR_IMPLEMENTATION_PLAN.md M2 step 6.
struct ViewportOverride
{
	int XB, YB, X, Y;
	const mat4* Projection = nullptr;
	WebGPUClipSpaceYConvention ClipSpaceYConvention = WebGPUClipSpaceYConvention::EngineProjection;
};

class VisibleFrame
{
public:
	void Process(const vec3& location, const mat4& worldToView, const Coords& viewRotation, bool mirrorFlag = false, int portalDepth = 0, const Array<PortalSpan>& portalSpans = {}, const vec4& portalPlane = vec4(0.0f, 0.0f, 0.0f, 1.0f), const ViewportOverride* viewportOverride = nullptr);
	void Draw();
	void DrawCoronas();

	RenderDevice* Device = nullptr;

	FSceneNode Frame;
	BspClipper Clipper;
	vec4 ViewLocation = vec4(0.0f);
	Coords ViewRotation = {};
	int ViewZone = 0;
	//uint64_t ViewZoneMask = 0;
	int FrameCounter = 0;
	bool MirrorFlag = false;
	int PortalDepth = 0;

	Array<VisibleNode> OpaqueNodes;
	Array<VisibleActor> Actors;
	Array<VisibleTranslucent> Translucents;
	Array<VisibleCorona> Coronas;
	Array<VisiblePortal> Portals;

private:
	void SetupSceneFrame(const mat4& worldToView, const ViewportOverride* viewportOverride);
	void ProcessNode(BspNode* node);
	void ProcessNodeSurface(BspNode* node, bool front);
	void SortTranslucent();

	void DrawOpaqueNodes();
	void DrawOpaqueActors();
	void DrawTranslucent();
	void DrawPortals();

	int FindZoneAt(const vec3& location);
	int FindZoneAt(const vec4& location, BspNode* node, BspNode* nodes);

	vec3 WarpLocationToOtherSide(UWarpZoneInfo* warpZone, vec3 p);
	vec3 WarpNormalToOtherSide(UWarpZoneInfo* warpZone, vec3 n);
	Coords WarpRotationToOtherSide(UWarpZoneInfo* warpZone, Coords rotation);
};
