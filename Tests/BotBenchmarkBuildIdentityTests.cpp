#include "BotBenchmark/BotBenchmarkBuildIdentity.h"

#include <iostream>

int main(int argc, char** argv)
{
	if (argc < 1 || !argv[0])
		return 1;
	std::string error;
	auto identity = BotBenchmarkBuildIdentity::TryCreate(
		"0123456789abcdef0123456789abcdef01234567",
		"89abcdef0123456789abcdef0123456789abcdef", false, argv[0], error);
	if (!identity)
	{
		std::cerr << error << '\n';
		return 1;
	}
	if (identity->ExecutableSizeBytes == 0 || identity->ExecutableSha256.size() != 64
		|| identity->IdentityId.size() != 71 || identity->ToJson().find("surreal-engine-build-identity-v1") == std::string::npos)
		return 1;
	std::string currentError;
	const auto current = BotBenchmarkBuildIdentity::TryCurrent(currentError);
	if (!current || current->ExecutableSizeBytes == 0 || current->ExecutableSha256.size() != 64)
	{
		std::cerr << currentError << '\n';
		return 1;
	}
	std::string invalidError;
	if (BotBenchmarkBuildIdentity::TryCreate("bad", identity->Tree, false, argv[0], invalidError))
		return 1;
	return 0;
}
