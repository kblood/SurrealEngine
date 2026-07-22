
#include "Precomp.h"
#include "UDXGameDirectory.h"
#include "DXSavePath.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include "Package/PackageManager.h"

void UDXGameDirectory::GetGameDirectory()
{
	if (GameDirectoryType() == EGameDirectoryTypes::GD_Maps)
	{
		currentDirectory = fs::path(engine->LaunchInfo.gameRootFolder) / "Maps";
		PopulateDirectoryList();
	}
	else
	{
		currentDirectory = engine->packages->GetSaveFolderPath();
		fs::create_directories(currentDirectory);
		PopulateSaveInfoPointers();
	}
}

int UDXGameDirectory::GetNewSaveFileIndex()
{
	const auto saveFolder = engine->packages->GetSaveFolderPath();

	// Save folders being formatted like Save0001 implies that the number can go up to 9999
	for (int i = 1 ; i < 10000 ; i++)
	{
		auto folderPath = saveFolder / GetSaveIndexFolderName(i);
		if (!fs::exists(folderPath) || (fs::exists(folderPath) && !fs::is_directory(folderPath)))
			return i;
	}

	return 0;
}

std::string UDXGameDirectory::GenerateSaveFilename(int saveIndex)
{
	return GetSaveIndexFolderName(saveIndex);
}

std::string UDXGameDirectory::GenerateNewSaveFileName(std::optional<int> newIndex)
{
	return GenerateSaveFilename(newIndex ? *newIndex : GetNewSaveFileIndex());
}

int UDXGameDirectory::GetDirCount()
{
	if (GameDirectoryType() == EGameDirectoryTypes::GD_SaveGames)
		return (int)LoadedSaveInfoPointers().size();

	return (int)DirectoryList().size();
}

std::string UDXGameDirectory::GetDirFilename(int fileIndex)
{
#if 0
	return DirectoryList()[fileIndex];
#else
	return {};
#endif
}

void UDXGameDirectory::SetDirType(EGameDirectoryTypes newDirType)
{
	GameDirectoryType() = newDirType;
}

void UDXGameDirectory::SetDirFilter(const std::string& strFilter)
{
	CurrentFilter() = strFilter;
}

UDXSaveInfo* UDXGameDirectory::GetSaveInfo(int fileIndex)
{
	if (GameDirectoryType() != EGameDirectoryTypes::GD_SaveGames)
		// We're not in the Save folder
		return nullptr;

	auto pkg = engine->packages->GetSaveInfoPackage(GetSaveIndexFolderName(fileIndex));

	if (!pkg)
		return nullptr;

	return Cast<UDXSaveInfo>(pkg->GetUObject("DeusExSaveInfo", "MyDeusExSaveInfo"));
}

UDXSaveInfo* UDXGameDirectory::GetSaveInfoFromDirectoryIndex(int DirectoryIndex)
{
	auto saveInfos = LoadedSaveInfoPointers();
	return DirectoryIndex >= 0 && (size_t)DirectoryIndex < saveInfos.size() ? saveInfos[(size_t)DirectoryIndex] : nullptr;
}

UDXSaveInfo* UDXGameDirectory::GetTempSaveInfo()
{
	return TempSaveInfo();
}

void UDXGameDirectory::DeleteSaveInfo(UDXSaveInfo& saveInfo)
{
	// The stock native releases the GameDirectory object's temporary ownership
	// of a loaded metadata object. PackageManager owns these objects in Surreal,
	// so there is no corresponding eager deletion to perform here.
}

void UDXGameDirectory::PurgeAllSaveInfo()
{
	LoadedSaveInfoPointers().Array->Resize(0);
}

int UDXGameDirectory::GetSaveFreeSpace()
{
	// The script return type is signed 32-bit KB. Cap before narrowing so large
	// modern volumes cannot wrap to a negative value.
	const uintmax_t freeSpaceInKBs = fs::space(currentDirectory).free / 1024;
	return static_cast<int>(std::min<uintmax_t>(freeSpaceInKBs, 1000ull * 1024 * 1024));
}

int UDXGameDirectory::GetSaveDirectorySize(int saveIndex)
{
	if (GameDirectoryType() != EGameDirectoryTypes::GD_SaveGames)
		// We're not in the Save folder
		return 0;

	int size = 0;
	const auto directory = currentDirectory / GetSaveIndexFolderName(saveIndex);
	if (!fs::exists(directory) || !fs::is_directory(directory))
		return 0;

	for (auto& p : fs::recursive_directory_iterator(directory))
		if (p.is_regular_file())
			size += (int)p.file_size();

	return size;
}

std::string UDXGameDirectory::GetSaveIndexFolderName(int saveIndex)
{
	return FormatDXSaveFolder(saveIndex);
}

void UDXGameDirectory::PopulateDirectoryList()
{
#if 0
	Array<std::string> newList;

	for (auto& p : fs::directory_iterator(currentDirectory))
		if (p.is_regular_file())
			newList.push_back(p.path().filename().string());

	DirectoryList() = newList;
#endif
}

void UDXGameDirectory::PopulateSaveInfoPointers()
{
	auto loaded = LoadedSaveInfoPointers();
	loaded.Array->Resize(0);
	for (const auto& saveInfoPackage : engine->packages->GetSaveInfoPackages())
	{
		auto saveInfo = TryCast<UDXSaveInfo>(saveInfoPackage.second->GetUObject("DeusExSaveInfo", "MyDeusExSaveInfo"));
		if (saveInfo && saveInfo->DirectoryIndex() >= 0)
			loaded.push_back(saveInfo);
	}

	std::sort(loaded.begin(), loaded.end(), [](UDXSaveInfo* left, UDXSaveInfo* right)
	{
		return left->DirectoryIndex() < right->DirectoryIndex();
	});
}
