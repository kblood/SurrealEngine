#pragma once

#include "Math/vec.h"

#include <cstdint>
#include <string>

namespace PawnMovement
{
	enum class UnrealScriptVectorOperation
	{
		AddVectorVector,
		AddEqualVectorVector,
		SubtractVectorVector,
		SubtractEqualVectorVector,
		SubtractPreVector,
		MultiplyVectorFloat,
		MultiplyFloatVector,
		MultiplyVectorVector,
		MultiplyEqualVectorFloat,
		MultiplyEqualVectorVector,
		DivideVectorFloat,
		DivideEqualVectorFloat,
		Normal
	};

	enum class VectorNonFiniteClass
	{
		Finite,
		NaN,
		NegativeInfinity,
		PositiveInfinity,
		Mixed
	};

	struct UnrealScriptVectorNonFiniteObservation
	{
		uint64_t Sequence = 0;
		uint64_t ObserverTick = 0;
		uint64_t CallerInvocationToken = 0;
		uint64_t SourceLifeId = 0;
		int32_t SourceActorIndex = -1;
		UnrealScriptVectorOperation Operation = UnrealScriptVectorOperation::AddVectorVector;
		VectorNonFiniteClass LeftVector = VectorNonFiniteClass::Finite;
		VectorNonFiniteClass RightVector = VectorNonFiniteClass::Finite;
		VectorNonFiniteClass Scalar = VectorNonFiniteClass::Finite;
		VectorNonFiniteClass ResultVector = VectorNonFiniteClass::Finite;
		bool RightVectorPresent = false;
		bool ScalarPresent = false;
		bool IntegrityValid = true;
		std::string CallerClass;
		std::string CallerFunction;
	};

	VectorNonFiniteClass ClassifyVectorNonFinite(const vec3& value);
	VectorNonFiniteClass ClassifyScalarNonFinite(float value);
	const char* UnrealScriptVectorOperationName(UnrealScriptVectorOperation value);
	const char* VectorNonFiniteClassName(VectorNonFiniteClass value);
	void ObserveUnrealScriptVectorOperation(UnrealScriptVectorOperation operation,
		const vec3& leftVector, const vec3* rightVector, const float* scalar,
		const vec3& resultVector);
}
