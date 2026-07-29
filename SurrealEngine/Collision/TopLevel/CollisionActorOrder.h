#pragma once

#include <string_view>

namespace CollisionActorOrder
{
	struct Key
	{
		bool HasActor = false;
		int LevelIndex = -1;
		std::string_view Name;
	};

	inline bool Less(const Key& left, const Key& right)
	{
		if (left.HasActor != right.HasActor)
			return !left.HasActor;
		if (!left.HasActor)
			return false;
		if (left.LevelIndex != right.LevelIndex)
			return left.LevelIndex < right.LevelIndex;
		return left.Name < right.Name;
	}
}
