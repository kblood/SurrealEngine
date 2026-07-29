#include "Collision/TopLevel/CollisionActorOrder.h"

#include <iostream>

namespace
{
	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	using CollisionActorOrder::Key;
	using CollisionActorOrder::Less;

	if (!Less({ false, -1, {} }, { true, 1, "Actor1" }))
		return Fail("world overlap did not sort before actor overlap");
	if (Less({ true, 1, "Actor1" }, { false, -1, {} }))
		return Fail("actor overlap sorted before world overlap");
	if (!Less({ true, 3, "LaterAddress" }, { true, 8, "EarlierAddress" }))
		return Fail("level actor index was not the primary actor ordering key");
	if (!Less({ true, 3, "ActorA" }, { true, 3, "ActorB" }))
		return Fail("actor name was not the deterministic duplicate-index fallback");
	if (Less({ true, 3, "ActorA" }, { true, 3, "ActorA" }))
		return Fail("equivalent actor keys did not compare equivalent");

	return 0;
}
