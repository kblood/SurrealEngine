#include "UObject/ActorMovement.h"

#include <iostream>
#include <string>

static int failures = 0;

static void Check(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
		failures++;
	}
}

int main()
{
	Check(!HasHorizontalMovement(0.0f, 0.0f), "stationary walking actor");
	Check(HasHorizontalMovement(1.0f, 0.0f), "X-only walking movement");
	Check(HasHorizontalMovement(0.0f, -1.0f), "Y-only walking movement");
	Check(HasHorizontalMovement(1.0f, 1.0f), "diagonal walking movement");

	Check(!HasSpatialMovement(0.0f, 0.0f, 0.0f), "stationary spatial actor");
	Check(HasSpatialMovement(1.0f, 0.0f, 0.0f), "X-only spatial movement");
	Check(HasSpatialMovement(0.0f, 1.0f, 0.0f), "Y-only spatial movement");
	Check(HasSpatialMovement(0.0f, 0.0f, -1.0f), "Z-only spatial movement");

	if (failures == 0)
		std::cout << "All actor movement tests passed.\n";
	return failures == 0 ? 0 : 1;
}
