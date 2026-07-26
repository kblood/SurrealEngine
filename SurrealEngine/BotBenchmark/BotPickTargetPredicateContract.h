#pragma once

// PickTarget currently retains the stock UE1 predicate: after excluding the
// caller, it considers dead pawns and skips living ones. Keep the predicate
// and benchmark provenance coupled here so observer evidence cannot be
// interpreted as evidence for the later fixed predicate by mistake.
namespace BotPickTargetPredicateContract
{
	enum class Mode
	{
		Stock,
		Fixed,
	};

	inline constexpr Mode CurrentMode = Mode::Stock;

	constexpr const char* ModeName(Mode mode)
	{
		switch (mode)
		{
		case Mode::Stock: return "stock";
		case Mode::Fixed: return "fixed";
		}
		return "unknown";
	}

	constexpr bool ShouldRejectPawn(Mode mode, bool isSelf, bool isLiving)
	{
		if (isSelf)
			return true;
		return mode == Mode::Stock ? isLiving : !isLiving;
	}

	constexpr bool ShouldRejectPawn(bool isSelf, bool isLiving)
	{
		return ShouldRejectPawn(CurrentMode, isSelf, isLiving);
	}
}
