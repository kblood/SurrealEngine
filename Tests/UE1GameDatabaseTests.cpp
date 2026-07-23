#include "Precomp.h"
#include "UE1GameDatabase.h"
#include "GameFolder.h"
#include "Engine.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
void Require(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << message << std::endl;
		std::exit(1);
	}
}

void CheckDescriptor(const char* hash, KnownUE1Games id, const char* executable, int ue1Version, int gameVersion, const char* versionString)
{
	const auto* descriptor = FindUE1GameCompatibilityDescriptorBySHA1(hash);
	Require(descriptor != nullptr, "demo hash did not resolve");
	Require(descriptor->id == id, "demo hash resolved to the wrong game");
	Require(std::string(descriptor->executableName) == executable, "demo executable name differs");
	Require(descriptor->ue1Version == ue1Version, "demo UE1 version differs");
	Require(descriptor->gameVersion == gameVersion, "demo game version differs");
	Require(std::string(descriptor->gameVersionString) == versionString, "demo version string differs");
	Require(descriptor->demo && descriptor->experimental, "unproven demo must remain explicitly experimental");
	Require(FindUE1GameCompatibilityDescriptor(id) == descriptor, "id and hash lookup disagree");
	Require(FindKnownUE1GameBySHA1(hash) == id, "native SHA1 lookup differs from descriptor");
}

void ScanAuditFolder(const char* path, KnownUE1Games expectedId, const char* expectedExecutable)
{
	std::cout << std::filesystem::path(path).filename().string() << ": starting read-only package scan" << std::endl;
	const auto detected = FindUE1GameInPath(path);
	Require(detected.first == expectedId && detected.second == expectedExecutable, "audit folder was not detected as expected");
	const auto* descriptor = FindUE1GameCompatibilityDescriptor(detected.first);
	Require(descriptor != nullptr, "detected demo has no compatibility descriptor");

	GameLaunchInfo launchInfo;
	launchInfo.gameRootFolder = path;
	launchInfo.gameExecutableName = std::filesystem::path(detected.second).stem().string();
	launchInfo.gameName = descriptor->gameName;
	launchInfo.ue1Version = descriptor->ue1Version;
	launchInfo.gameVersion = descriptor->gameVersion;
	launchInfo.gameSubVersion = descriptor->gameSubVersion;
	launchInfo.gameVersionString = descriptor->gameVersionString;
	launchInfo.demo = descriptor->demo;
	launchInfo.experimentalCompatibility = descriptor->experimental;

	Engine engineInstance(launchInfo);
	std::cout << std::filesystem::path(path).filename().string() << ": detection and package scan passed" << std::endl;
}

int RunIsolatedAuditFolder(const char* executable, const char* path)
{
	// Engine construction registers process-global VM native functions. Run each
	// real-data scan in a fresh process so one game's native table cannot leak
	// into the next scan and make the result depend on argument order.
	std::ostringstream command;
#ifdef _WIN32
	// cmd.exe requires an extra enclosing pair when the command itself starts
	// with a quoted executable path.
	command << "\"\"" << executable << "\" \"" << path << "\"\"";
#else
	command << '"' << executable << "\" \"" << path << '"';
#endif
	return std::system(command.str().c_str());
}
}

int main(int argc, char** argv)
{
	CheckDescriptor("4bb5e71f78cf4806d9240df01f72236134af4a31", KnownUE1Games::UT99_348_DEMO, "UnrealTournament.exe", 348, 348, "348demo");
	CheckDescriptor("b851dcc69c4f773252c0498bd12756d90bcb59c2", KnownUE1Games::UNREAL_200_DEMO, "Unreal.exe", 200, 200, "200");
	CheckDescriptor("4be582d4194400e87f64894c92b3f2119e012251", KnownUE1Games::DEUS_EX_1002f_DEMO, "DeusEx.exe", 500, 1002, "1002f_DEMO");

	// Preserve the existing retail identity next to the demo identity.
	Require(FindKnownUE1GameBySHA1("9f923d667a396e8243028c14dc3f5e0a6db13d84") == KnownUE1Games::DEUS_EX_1002f,
		"retail Deus Ex 1002f hash changed");
	Require(FindUE1GameCompatibilityDescriptorBySHA1("9f923d667a396e8243028c14dc3f5e0a6db13d84") == nullptr,
		"retail executable was mislabeled as a demo");

	// Optional read-only validation against locally extracted audit folders.
	if (argc == 4)
	{
		for (int index = 1; index != 4; index++)
		{
			if (RunIsolatedAuditFolder(argv[0], argv[index]) != 0)
			{
				std::cerr << "isolated package scan failed for " << argv[index] << std::endl;
				return 2;
			}
		}
	}
	else if (argc == 2)
	{
		try
		{
			const auto detected = FindUE1GameInPath(argv[1]);
			if (detected.first == KnownUE1Games::UT99_348_DEMO)
				ScanAuditFolder(argv[1], detected.first, "UnrealTournament.exe");
			else if (detected.first == KnownUE1Games::UNREAL_200_DEMO)
				ScanAuditFolder(argv[1], detected.first, "Unreal.exe");
			else if (detected.first == KnownUE1Games::DEUS_EX_1002f_DEMO)
				ScanAuditFolder(argv[1], detected.first, "DeusEx.exe");
			else
				Require(false, "single audit folder is not a supported demo");
		}
		catch (const std::exception& error)
		{
			std::cerr << "package scan failed: " << error.what() << std::endl;
			return 2;
		}
	}

	std::cout << "UE1 demo compatibility descriptors passed" << std::endl;
	return 0;
}
