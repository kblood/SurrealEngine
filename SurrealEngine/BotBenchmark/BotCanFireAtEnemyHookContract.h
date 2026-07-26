#pragma once

#include <string>
#include <vector>

namespace BotCanFireAtEnemyHookContract
{
	struct SignatureShape
	{
		std::string FunctionName;
		std::vector<std::string> Parameters;
		bool ReturnsBoolean = false;
	};

	// This observer is deliberately bound only to Botpack.Bot's no-argument
	// script predicate. A changed contract disables observation rather than
	// guessing at a game's firing semantics.
	std::string ValidateCanFireAtEnemySignature(const SignatureShape& signature);

	struct TraceSignatureShape
	{
		std::string FunctionName;
		std::vector<std::string> Parameters;
		bool ReturnsObject = false;
	};

	// The nested witness is the native Engine.Actor Trace declaration used by
	// the retail Botpack predicate.  It is validated separately so a later
	// engine variant cannot be mistaken for the six-argument UT99 call.
	std::string ValidateTraceSignature(const TraceSignatureShape& signature);
}
