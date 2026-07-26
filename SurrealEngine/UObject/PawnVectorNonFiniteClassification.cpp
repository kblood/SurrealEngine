#include "PawnVectorNonFiniteObserver.h"

#include <cmath>

namespace
{
	PawnMovement::VectorNonFiniteClass ClassifyComponent(float value)
	{
		if (std::isfinite(value))
			return PawnMovement::VectorNonFiniteClass::Finite;
		if (std::isnan(value))
			return PawnMovement::VectorNonFiniteClass::NaN;
		return value < 0.0f ? PawnMovement::VectorNonFiniteClass::NegativeInfinity
			: PawnMovement::VectorNonFiniteClass::PositiveInfinity;
	}
}

namespace PawnMovement
{
	VectorNonFiniteClass ClassifyVectorNonFinite(const vec3& value)
	{
		const VectorNonFiniteClass x = ClassifyComponent(value.x);
		const VectorNonFiniteClass y = ClassifyComponent(value.y);
		const VectorNonFiniteClass z = ClassifyComponent(value.z);
		if (x == y && y == z)
			return x;
		if (x == VectorNonFiniteClass::Finite && y == VectorNonFiniteClass::Finite
			&& z == VectorNonFiniteClass::Finite)
		{
			return VectorNonFiniteClass::Finite;
		}
		return VectorNonFiniteClass::Mixed;
	}

	VectorNonFiniteClass ClassifyScalarNonFinite(float value)
	{
		return ClassifyComponent(value);
	}

	const char* UnrealScriptVectorOperationName(UnrealScriptVectorOperation value)
	{
		switch (value)
		{
		case UnrealScriptVectorOperation::AddVectorVector: return "add_vector_vector";
		case UnrealScriptVectorOperation::AddEqualVectorVector: return "add_equal_vector_vector";
		case UnrealScriptVectorOperation::SubtractVectorVector: return "subtract_vector_vector";
		case UnrealScriptVectorOperation::SubtractEqualVectorVector: return "subtract_equal_vector_vector";
		case UnrealScriptVectorOperation::SubtractPreVector: return "subtract_pre_vector";
		case UnrealScriptVectorOperation::MultiplyVectorFloat: return "multiply_vector_float";
		case UnrealScriptVectorOperation::MultiplyFloatVector: return "multiply_float_vector";
		case UnrealScriptVectorOperation::MultiplyVectorVector: return "multiply_vector_vector";
		case UnrealScriptVectorOperation::MultiplyEqualVectorFloat: return "multiply_equal_vector_float";
		case UnrealScriptVectorOperation::MultiplyEqualVectorVector: return "multiply_equal_vector_vector";
		case UnrealScriptVectorOperation::DivideVectorFloat: return "divide_vector_float";
		case UnrealScriptVectorOperation::DivideEqualVectorFloat: return "divide_equal_vector_float";
		case UnrealScriptVectorOperation::Normal: return "normal";
		}
		return "unknown";
	}

	const char* VectorNonFiniteClassName(VectorNonFiniteClass value)
	{
		switch (value)
		{
		case VectorNonFiniteClass::Finite: return "finite";
		case VectorNonFiniteClass::NaN: return "nan";
		case VectorNonFiniteClass::NegativeInfinity: return "negative_infinity";
		case VectorNonFiniteClass::PositiveInfinity: return "positive_infinity";
		case VectorNonFiniteClass::Mixed: return "mixed";
		}
		return "unknown";
	}
}
