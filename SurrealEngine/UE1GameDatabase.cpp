
#include "Precomp.h"
#include "UE1GameDatabase.h"
#include "Utils/SHA1Sum.h"
#include <iostream>

static const UE1GameCompatibilityDescriptor DemoCompatibilityDescriptors[] = {
	{ KnownUE1Games::UNREAL_200_DEMO, "b851dcc69c4f773252c0498bd12756d90bcb59c2", "Unreal.exe", "Unreal Special Edition Demo (experimental)", 200, 200, 0, "200", true, true },
	{ KnownUE1Games::UT99_348_DEMO, "4bb5e71f78cf4806d9240df01f72236134af4a31", "UnrealTournament.exe", "Unreal Tournament Demo (experimental)", 348, 348, 0, "348demo", true, true },
	{ KnownUE1Games::DEUS_EX_1002f_DEMO, "4be582d4194400e87f64894c92b3f2119e012251", "DeusEx.exe", "Deus Ex Demo (experimental)", 500, 1002, 0, "1002f_DEMO", true, true },
};

const UE1GameCompatibilityDescriptor* FindUE1GameCompatibilityDescriptor(KnownUE1Games id)
{
	for (const auto& descriptor : DemoCompatibilityDescriptors)
	{
		if (descriptor.id == id)
			return &descriptor;
	}
	return nullptr;
}

const UE1GameCompatibilityDescriptor* FindUE1GameCompatibilityDescriptorBySHA1(const std::string& sha1)
{
	for (const auto& descriptor : DemoCompatibilityDescriptors)
	{
		if (sha1 == descriptor.executableSHA1)
			return &descriptor;
	}
	return nullptr;
}

KnownUE1Games FindKnownUE1GameBySHA1(const std::string& sha1)
{
	if (const auto* compatibility = FindUE1GameCompatibilityDescriptorBySHA1(sha1))
		return compatibility->id;
	const auto known = SHA1Database.find(sha1);
	return known != SHA1Database.end() ? known->second : KnownUE1Games::UE1_GAME_NOT_FOUND;
}

std::pair<KnownUE1Games, std::string> FindUE1GameInPath(const std::string& ue1_game_root_folder_path)
{
	if (ue1_game_root_folder_path.empty())
		return std::make_pair(KnownUE1Games::UE1_GAME_NOT_FOUND, "");

	if (!fs::exists(ue1_game_root_folder_path) || !fs::is_directory(ue1_game_root_folder_path))
		return std::make_pair(KnownUE1Games::UE1_GAME_NOT_FOUND, "");

	const auto UE1GameSystemPath = fs::path(ue1_game_root_folder_path) / "System";
	const auto UE1GameSystem64Path = fs::path(ue1_game_root_folder_path) / "System64";

	for (auto& executable_name : knownUE1ExecutableNames)
	{
		auto executablePath = UE1GameSystemPath / executable_name;
		auto executablePath64 = UE1GameSystem64Path / executable_name;

		std::string sha1sum = SHA1Sum::of_file(executablePath);
		std::string sha1sum64 = SHA1Sum::of_file(executablePath64);

		// If sha1sum is empty this means that the file doesn't exist
		if (sha1sum.empty() && sha1sum64.empty())
			continue;

		// Now check whether there is a match within the database or not
		const KnownUE1Games knownGame = FindKnownUE1GameBySHA1(!sha1sum.empty() ? sha1sum : sha1sum64);
		if (knownGame == KnownUE1Games::UE1_GAME_NOT_FOUND)
			return std::make_pair(KnownUE1Games::UE1_GAME_NOT_FOUND, "");

		// Hack: Tactical-Ops has a version that contains the exact same UTv469d executable. Handle that here
		if (knownGame == KnownUE1Games::UT99_469d && executable_name == "TacticalOps.exe")
			return std::make_pair(KnownUE1Games::TACTICAL_OPS_469, executable_name);
		return std::make_pair(knownGame, executable_name);
	}

	// We got nothing here
	return std::make_pair(KnownUE1Games::UE1_GAME_NOT_FOUND, "");
}
