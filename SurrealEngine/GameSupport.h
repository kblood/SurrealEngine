#pragma once

#include <cstdint>
#include <string_view>

struct GameLaunchInfo;

enum class GameId
{
	Unknown,
	Unreal,
	UnrealTournament,
	DeusEx
};

enum class GameCapability : uint32_t
{
	None = 0,
	SaveInfoPackages = 1 << 0,
	PostPostBeginPlayEvent = 1 << 1
};

// Describes behavior selected by the game executable. Capabilities should name
// engine behavior rather than a particular game so call sites stay generic.
class GameSupport
{
public:
	GameId Id() const { return id; }
	std::string_view ExecutableName() const { return executableName; }
	bool HasCapability(GameCapability capability) const;
	void RegisterNativeFunctions(const GameLaunchInfo& launchInfo) const;

private:
	using NativeFunctionRegistration = void (*)(const GameLaunchInfo& launchInfo);

	constexpr GameSupport(GameId id, std::string_view executableName, uint32_t capabilities, NativeFunctionRegistration registerNativeFunctions)
		: id(id), executableName(executableName), capabilities(capabilities), registerNativeFunctions(registerNativeFunctions) { }

	GameId id = GameId::Unknown;
	std::string_view executableName;
	uint32_t capabilities = 0;
	NativeFunctionRegistration registerNativeFunctions = nullptr;

	friend class GameSupportRegistry;
};

class GameSupportRegistry
{
public:
	static const GameSupport& Find(std::string_view executableName);
};
