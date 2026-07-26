#include "BotBenchmark/BotPickTargetPredicateContract.h"

#include <iostream>
#include <string_view>

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
	using namespace BotPickTargetPredicateContract;
	if (CurrentMode != Mode::Stock || std::string_view(ModeName(CurrentMode)) != "stock")
		return Fail("the active PickTarget predicate mode was not stock");
	if (!ShouldRejectPawn(Mode::Stock, true, false)
		|| !ShouldRejectPawn(Mode::Stock, false, true)
		|| ShouldRejectPawn(Mode::Stock, false, false))
	{
		return Fail("stock PickTarget predicate classification was incorrect");
	}
	if (!ShouldRejectPawn(Mode::Fixed, true, true)
		|| !ShouldRejectPawn(Mode::Fixed, false, false)
		|| ShouldRejectPawn(Mode::Fixed, false, true))
	{
		return Fail("fixed PickTarget predicate classification was incorrect");
	}
	if (ShouldRejectPawn(false, false) || !ShouldRejectPawn(false, true))
		return Fail("active PickTarget predicate diverged from its reported mode");
	return 0;
}
