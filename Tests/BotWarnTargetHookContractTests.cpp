#include "BotBenchmark/BotWarnTargetHookContract.h"

#include <iostream>

namespace
{
	using namespace BotWarnTargetHookContract;

	int Fail(const char* message)
	{
		std::cerr << message << '\n';
		return 1;
	}

	SignatureShape WarnTargetSignature()
	{
		return { "WarnTarget", {
			{ "shooter", ValueKind::Object, "Pawn" },
			{ "projSpeed", ValueKind::Float, {} },
			{ "FireDir", ValueKind::Vector, {} },
		}, {} };
	}

	SignatureShape TryToDuckSignature()
	{
		return { "TryToDuck", {
			{ "duckDir", ValueKind::Vector, {} },
			{ "bReversed", ValueKind::Boolean, {} },
		}, {} };
	}
}

int main()
{
	auto warn = WarnTargetSignature();
	if (!ValidateWarnTargetSignature(warn).empty())
		return Fail("canonical WarnTarget contract was rejected");
	warn.FunctionName = "wArNtArGeT";
	warn.Parameters[0].Name = "ShOoTeR";
	warn.Parameters[0].ObjectClass = "pAwN";
	if (!ValidateWarnTargetSignature(warn).empty())
		return Fail("WarnTarget did not honor Unreal name case-insensitivity");
	warn = WarnTargetSignature();
	warn.Parameters[1].Kind = ValueKind::Boolean;
	if (ValidateWarnTargetSignature(warn).empty())
		return Fail("WarnTarget accepted a non-float projectile speed");
	warn = WarnTargetSignature();
	warn.ReturnValue = ParameterShape{ "ReturnValue", ValueKind::Boolean, {} };
	if (ValidateWarnTargetSignature(warn).empty())
		return Fail("WarnTarget accepted a return value");

	auto duck = TryToDuckSignature();
	if (!ValidateTryToDuckSignature(duck).empty())
		return Fail("canonical TryToDuck contract was rejected");
	duck.Parameters[1].Name = "Reversed";
	if (ValidateTryToDuckSignature(duck).empty())
		return Fail("TryToDuck accepted an unexpected bool parameter name");
	duck = TryToDuckSignature();
	duck.Parameters.push_back({ "extra", ValueKind::Float, {} });
	if (ValidateTryToDuckSignature(duck).empty())
		return Fail("TryToDuck accepted an extra parameter");
	return 0;
}
