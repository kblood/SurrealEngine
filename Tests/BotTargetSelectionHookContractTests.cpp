#include "BotBenchmark/BotTargetSelectionHookContract.h"

#include <iostream>

namespace
{
	using namespace BotTargetSelectionHookContract;

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	SignatureShape CanonicalSignature()
	{
		return { "SetEnemy", { { "NewEnemy", ValueKind::Object, "Pawn" } },
			ParameterShape{ "ReturnValue", ValueKind::Boolean } };
	}
}

int main()
{
	auto signature = CanonicalSignature();
	if (!ValidateSetEnemySignature(signature).empty())
		return Fail("canonical SetEnemy contract was rejected");

	signature.FunctionName = "sEtEnEmY";
	signature.Parameters[0].Name = "nEwEnEmY";
	signature.Parameters[0].ObjectClass = "pAwN";
	signature.ReturnValue->Name = "rEtUrNvAlUe";
	if (!ValidateSetEnemySignature(signature).empty())
		return Fail("Unreal name case-insensitivity was not honored");

	signature = CanonicalSignature();
	signature.Parameters[0].ObjectClass = "Actor";
	if (ValidateSetEnemySignature(signature).empty())
		return Fail("non-Pawn SetEnemy parameter was accepted");

	signature = CanonicalSignature();
	signature.Parameters[0].Name = "Enemy";
	if (ValidateSetEnemySignature(signature).empty())
		return Fail("wrong SetEnemy parameter name was accepted");

	signature = CanonicalSignature();
	signature.Parameters.push_back({ "Extra", ValueKind::Object, "Pawn" });
	if (ValidateSetEnemySignature(signature).empty())
		return Fail("SetEnemy signature with an extra parameter was accepted");

	signature = CanonicalSignature();
	signature.ReturnValue.reset();
	if (ValidateSetEnemySignature(signature).empty())
		return Fail("SetEnemy signature without a bool return was accepted");

	signature = CanonicalSignature();
	signature.ReturnValue->Kind = ValueKind::Object;
	if (ValidateSetEnemySignature(signature).empty())
		return Fail("SetEnemy signature with a non-bool return was accepted");

	return 0;
}
