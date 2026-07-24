#include "BotBenchmark/BotBenchmarkDeathAttributionHookContract.h"

#include <iostream>

namespace
{
	using namespace BotBenchmarkDeathAttribution;

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}
}

int main()
{
	HookSignatureShape damage{ "TakeDamage", {
		{ "Damage", HookValueKind::Integer },
		{ "instigatedBy", HookValueKind::Object, "Pawn" },
		{ "hitlocation", HookValueKind::Vector },
		{ "momentum", HookValueKind::Vector },
		{ "damageType", HookValueKind::Name },
	} };
	if (!ValidateHookContract(HookPoint::TakeDamage, damage).empty())
		return Fail("canonical TakeDamage contract was rejected");

	HookSignatureShape killed{ "killed", {
		{ "killer", HookValueKind::Object, "pawn" },
		{ "OTHER", HookValueKind::Object, "PAWN" },
		{ "DamageType", HookValueKind::Name },
	} };
	if (!ValidateHookContract(HookPoint::Killed, killed).empty())
		return Fail("Unreal name case-insensitivity was not honored");

	for (const auto& valid : {
		std::pair{ HookPoint::AddVelocity, HookSignatureShape{ "AddVelocity", {
			{ "NewVelocity", HookValueKind::Vector } } } },
		std::pair{ HookPoint::PainTimer, HookSignatureShape{ "PainTimer" } },
		std::pair{ HookPoint::FellOutOfWorld, HookSignatureShape{ "FellOutOfWorld" } },
		std::pair{ HookPoint::TakeFallingDamage, HookSignatureShape{ "TakeFallingDamage" } },
		std::pair{ HookPoint::Landed, HookSignatureShape{ "Landed", {
			{ "HitNormal", HookValueKind::Vector } } } },
	})
	{
		if (!ValidateHookContract(valid.first, valid.second).empty())
			return Fail("supported hook contract was rejected");
	}

	HookSignatureShape encroaching{ "EncroachingOn", {
		{ "Other", HookValueKind::Object, "Actor" },
	}, HookParameterShape{ "ReturnValue", HookValueKind::Boolean } };
	if (!ValidateHookContract(HookPoint::MoverEncroachingOn, encroaching).empty())
		return Fail("Mover.EncroachingOn contract was rejected");

	auto wrong = damage;
	wrong.Parameters[1].ObjectClass = "Actor";
	if (ValidateHookContract(HookPoint::TakeDamage, wrong).empty())
		return Fail("wrong TakeDamage object type was accepted");
	wrong = damage;
	wrong.Parameters.erase(wrong.Parameters.begin());
	if (ValidateHookContract(HookPoint::TakeDamage, wrong).empty())
		return Fail("truncated TakeDamage contract was accepted");
	auto wrongReturn = encroaching;
	wrongReturn.ReturnValue.reset();
	if (ValidateHookContract(HookPoint::MoverEncroachingOn, wrongReturn).empty())
		return Fail("missing EncroachingOn return was accepted");
	return 0;
}
