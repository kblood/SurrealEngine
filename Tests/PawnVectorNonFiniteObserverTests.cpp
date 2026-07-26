#include "UObject/PawnVectorNonFiniteObserver.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace
{
	bool Check(bool condition, const std::string& message)
	{
		if (!condition)
			std::cerr << "Pawn vector non-finite observer test failed: " << message << '\n';
		return condition;
	}
}

int main()
{
	using PawnMovement::ClassifyScalarNonFinite;
	using PawnMovement::ClassifyVectorNonFinite;
	using PawnMovement::UnrealScriptVectorOperation;
	using PawnMovement::UnrealScriptVectorOperationName;
	using PawnMovement::VectorNonFiniteClass;
	using PawnMovement::VectorNonFiniteClassName;

	bool ok = true;
	ok &= Check(ClassifyVectorNonFinite(vec3(1.0f, 2.0f, 3.0f))
		== VectorNonFiniteClass::Finite, "finite vector classification changed");
	ok &= Check(ClassifyScalarNonFinite(std::numeric_limits<float>::quiet_NaN())
		== VectorNonFiniteClass::NaN, "NaN scalar classification changed");
	ok &= Check(ClassifyScalarNonFinite(-std::numeric_limits<float>::infinity())
		== VectorNonFiniteClass::NegativeInfinity,
		"negative infinity scalar classification changed");
	ok &= Check(ClassifyVectorNonFinite(vec3(1.0f,
		std::numeric_limits<float>::infinity(), 3.0f)) == VectorNonFiniteClass::Mixed,
		"mixed vector classification changed");
	ok &= Check(std::string(VectorNonFiniteClassName(VectorNonFiniteClass::PositiveInfinity))
		== "positive_infinity", "positive infinity class name changed");
	ok &= Check(std::string(UnrealScriptVectorOperationName(
		UnrealScriptVectorOperation::Normal)) == "normal", "normal operation name changed");
	ok &= Check(std::string(UnrealScriptVectorOperationName(
		UnrealScriptVectorOperation::DivideVectorFloat)) == "divide_vector_float",
		"divide operation name changed");
	return ok ? 0 : 1;
}
