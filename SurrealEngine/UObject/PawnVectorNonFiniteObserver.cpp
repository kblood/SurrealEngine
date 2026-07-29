#include "PawnVectorNonFiniteObserver.h"

#include "Engine.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "VM/Frame.h"

namespace PawnMovement
{
	void ObserveUnrealScriptVectorOperation(UnrealScriptVectorOperation operation,
		const vec3& leftVector, const vec3* rightVector, const float* scalar,
		const vec3& resultVector)
	{
		if (!engine || !engine->IsBotBenchmarkVectorNonFiniteObserverEnabled()
			|| Frame::Callstack.size() < 2)
		{
			return;
		}

		Frame* caller = Frame::Callstack[Frame::Callstack.size() - 2];
		UPawn* pawn = caller ? UObject::TryCast<UPawn>(caller->Object) : nullptr;
		if (!pawn)
			return;

		const VectorNonFiniteClass leftClass = ClassifyVectorNonFinite(leftVector);
		const VectorNonFiniteClass rightClass = rightVector
			? ClassifyVectorNonFinite(*rightVector) : VectorNonFiniteClass::Finite;
		const VectorNonFiniteClass scalarClass = scalar
			? ClassifyScalarNonFinite(*scalar) : VectorNonFiniteClass::Finite;
		const VectorNonFiniteClass resultClass = ClassifyVectorNonFinite(resultVector);
		if (leftClass == VectorNonFiniteClass::Finite
			&& rightClass == VectorNonFiniteClass::Finite
			&& scalarClass == VectorNonFiniteClass::Finite
			&& resultClass == VectorNonFiniteClass::Finite)
		{
			return;
		}

		UnrealScriptVectorNonFiniteObservation observation;
		observation.ObserverTick = engine->BotBenchmarkObserverTick();
		observation.CallerInvocationToken = caller->EnsureInvocationToken();
		observation.SourceLifeId = pawn->DirectReachCommandLifeId();
		observation.SourceActorIndex = pawn->Index;
		observation.Operation = operation;
		observation.LeftVector = leftClass;
		observation.RightVector = rightClass;
		observation.Scalar = scalarClass;
		observation.ResultVector = resultClass;
		observation.RightVectorPresent = rightVector != nullptr;
		observation.ScalarPresent = scalar != nullptr;
		if (caller->Object)
			observation.CallerClass = UObject::GetUClassFullName(caller->Object).ToString();
		if (caller->Func)
			observation.CallerFunction = caller->Func->Name.ToString();
		observation.IntegrityValid = observation.CallerInvocationToken != 0
			&& observation.SourceActorIndex >= 0 && !observation.CallerClass.empty()
			&& !observation.CallerFunction.empty();
		pawn->RecordUnrealScriptVectorNonFiniteObservation(std::move(observation));
	}
}
