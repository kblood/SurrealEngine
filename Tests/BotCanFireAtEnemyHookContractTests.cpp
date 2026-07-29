#include "BotBenchmark/BotCanFireAtEnemyHookContract.h"

#include <cassert>

int main()
{
	using namespace BotCanFireAtEnemyHookContract;
	assert(ValidateCanFireAtEnemySignature({ "CanFireAtEnemy", {}, true }).empty());
	assert(!ValidateCanFireAtEnemySignature({ "CanFireAtEnemy", { "x" }, true }).empty());
	assert(!ValidateCanFireAtEnemySignature({ "CanFireAtEnemy", {}, false }).empty());
	assert(!ValidateCanFireAtEnemySignature({ "Other", {}, true }).empty());
	assert(ValidateTraceSignature({ "Trace", { "vector", "vector", "vector", "vector", "bool", "vector" }, true }).empty());
	assert(!ValidateTraceSignature({ "Trace", { "vector" }, true }).empty());
	assert(!ValidateTraceSignature({ "Trace", { "vector", "vector", "vector", "vector", "bool", "vector" }, false }).empty());
}
