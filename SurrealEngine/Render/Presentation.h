#pragma once

#include "Utils/Array.h"

#include <cstdint>

// Logical render layers are deliberately provider-neutral. A presentation-aware
// backend can route them to separate textures or compositor layers; ordinary
// render devices accept enabled layers assigned to the default window target.
enum class PresentationLayer
{
	World,
	WeaponOverlay,
	UserInterface,
	XRUIVisualOverlay,
	Cinematic
};

// Target slots are local to one presentation plan. Slot zero always means the
// render device's default/window target. The owner of a presentation-aware
// render device defines and registers any non-zero slots.
struct PresentationTarget
{
	uint32_t Slot = 0;

	bool IsDefault() const { return Slot == 0; }
	bool operator==(const PresentationTarget&) const = default;
};

// Opaque images are registered for a target slot by the provider that owns
// their acquire/release lifecycle. The render backend alone interprets the
// native handles; engine and presentation code only route the slot.
struct PresentationTargetImage
{
	void* NativeHandle = nullptr;
	int Width = 0;
	int Height = 0;
	// Some compositor coordinate systems reflect the canvas U axis relative to
	// the engine surface basis. The backend applies this during the final copy.
	bool FlipHorizontal = false;
};

struct PresentationTargetBinding
{
	PresentationTarget Target;
	Array<PresentationTargetImage> Images;
};

struct PresentationLayerDescription
{
	PresentationLayer Layer = PresentationLayer::World;
	PresentationTarget Target;
	bool Enabled = true;
};

// Missing entries resolve to an enabled layer on the default target. This is
// the compatibility contract that keeps existing one-window rendering intact.
struct PresentationPlan
{
	PresentationLayerDescription GetLayer(PresentationLayer layer) const
	{
		for (const PresentationLayerDescription& description : Layers)
		{
			if (description.Layer == layer)
				return description;
		}
		return { layer, {}, true };
	}

	void SetLayer(PresentationLayer layer, PresentationTarget target, bool enabled = true)
	{
		for (PresentationLayerDescription& description : Layers)
		{
			if (description.Layer == layer)
			{
				description.Target = target;
				description.Enabled = enabled;
				return;
			}
		}
		Layers.push_back({ layer, target, enabled });
	}

	Array<PresentationLayerDescription> Layers;
};
