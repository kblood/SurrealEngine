#include "Precomp.h"
#include "BrowserGameDataMount.h"

#include <emscripten.h>
#include <emscripten/heap.h>
#include <emscripten/wasmfs.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
	constexpr int MountABIVersion = 2;
	constexpr const char* OPFSMountRoot = "/.surreal-opfs";
	constexpr const char* GameDataRoot = "/gamedata";

	struct MountEntry
	{
		std::string Path;
		uint64_t Size = 0;
	};

	struct MountConfiguration
	{
		std::string StorageDirectory;
		std::string DatasetId;
		uint32_t ExpectedFiles = 0;
		std::vector<MountEntry> Files;
		bool Begun = false;
		bool Mounted = false;
		std::string Error;
	};

	MountConfiguration Configuration;

	bool IsSafeComponent(const std::string& value)
	{
		if (value.empty() || value == "." || value == "..")
			return false;
		return std::all_of(value.begin(), value.end(), [](unsigned char ch)
		{
			return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
				(ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
		});
	}

	bool IsSafeRelativePath(const std::string& value)
	{
		if (value.empty() || value.front() == '/' || value.back() == '/' ||
			value.find('\\') != std::string::npos || value.find('\0') != std::string::npos)
			return false;
		size_t start = 0;
		while (start < value.size())
		{
			const size_t end = value.find('/', start);
			const std::string component = value.substr(start,
				end == std::string::npos ? std::string::npos : end - start);
			if (component.empty() || component == "." || component == ".." ||
				std::any_of(component.begin(), component.end(),
					[](unsigned char ch) { return ch < 32 || ch == 127; }))
				return false;
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		return true;
	}

	std::string LowerASCII(std::string value)
	{
		for (char& ch : value)
		{
			if (ch >= 'A' && ch <= 'Z')
				ch = static_cast<char>(ch - 'A' + 'a');
		}
		return value;
	}

	bool IsMutableGamePath(const std::string& relativePath)
	{
		const std::string path = LowerASCII(relativePath);
		if (path == "system/se-unrealtournament.ini" || path == "system/se-unreal.ini" ||
			path == "system/se-deusex.ini" || path == "system/se-user.ini")
			return true;
		constexpr const char* SavePrefix = "save/save";
		if (path.rfind(SavePrefix, 0) != 0)
			return false;
		const size_t extension = path.find('.', std::strlen(SavePrefix));
		if (extension == std::string::npos || extension == std::strlen(SavePrefix))
			return false;
		if (!std::all_of(path.begin() + std::strlen(SavePrefix), path.begin() + extension,
			[](unsigned char ch) { return ch >= '0' && ch <= '9'; }))
			return false;
		return path.substr(extension) == ".usa" || path.substr(extension) == ".dxs";
	}

	bool PathExists(const std::string& path, struct stat* result = nullptr)
	{
		struct stat info = {};
		if (lstat(path.c_str(), &info) != 0)
			return false;
		if (result)
			*result = info;
		return true;
	}

	bool EnsureDirectory(const std::string& path, mode_t mode)
	{
		if (path.empty() || path == "/")
			return true;
		std::string current;
		for (size_t start = 1; start <= path.size();)
		{
			const size_t end = path.find('/', start);
			current += "/" + path.substr(start,
				end == std::string::npos ? std::string::npos : end - start);
			struct stat info = {};
			if (lstat(current.c_str(), &info) == 0)
			{
				if (!S_ISDIR(info.st_mode))
					return false;
			}
			else if (errno != ENOENT || mkdir(current.c_str(), mode) != 0)
			{
				return false;
			}
			if (end == std::string::npos)
				break;
			start = end + 1;
		}
		return true;
	}

	std::string ParentPath(const std::string& path)
	{
		const size_t slash = path.find_last_of('/');
		return slash == std::string::npos ? std::string() : path.substr(0, slash);
	}

	bool CopyMutableBaseline(const std::string& source, const std::string& destination)
	{
		struct stat existing = {};
		if (PathExists(destination, &existing))
			return S_ISREG(existing.st_mode) && !S_ISLNK(existing.st_mode);

		const int input = open(source.c_str(), O_RDONLY);
		if (input < 0)
			return false;
		const int output = open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (output < 0)
		{
			close(input);
			return false;
		}

		std::vector<uint8_t> buffer(1024 * 1024);
		bool success = true;
		while (success)
		{
			const ssize_t received = read(input, buffer.data(), buffer.size());
			if (received < 0)
			{
				success = false;
				break;
			}
			if (received == 0)
				break;
			ssize_t offset = 0;
			while (offset < received)
			{
				const ssize_t written = write(output, buffer.data() + offset,
					static_cast<size_t>(received - offset));
				if (written <= 0)
				{
					success = false;
					break;
				}
				offset += written;
			}
		}
		success = close(input) == 0 && success;
		success = close(output) == 0 && success;
		if (!success)
			unlink(destination.c_str());
		return success;
	}

	bool Fail(const std::string& message)
	{
		Configuration.Error = message;
		return false;
	}
}

extern "C"
{
	EMSCRIPTEN_KEEPALIVE int Surreal_GetBrowserOPFSMountABIVersion()
	{
		return MountABIVersion;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_GetBrowserOPFSMountMode()
	{
#ifdef SURREAL_WEB_WASMFS_OPFS_ASYNCIFY
		return 2; // Window-owned engine; JavaScript awaits the native OPFS preparation.
#else
		return 1; // PROXY_TO_PTHREAD engine; GameApp prepares OPFS on its worker.
#endif
	}

	EMSCRIPTEN_KEEPALIVE uint32_t Surreal_GetBrowserWasmHeapSize()
	{
		return static_cast<uint32_t>(emscripten_get_heap_size());
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_BeginBrowserOPFSMount(const char* storageDirectory,
		const char* datasetId, uint32_t expectedFiles)
	{
		if (!storageDirectory || !datasetId || !IsSafeComponent(storageDirectory) ||
			!IsSafeComponent(datasetId) || expectedFiles == 0 || Configuration.Mounted)
			return 0;
		Configuration = {};
		Configuration.StorageDirectory = storageDirectory;
		Configuration.DatasetId = datasetId;
		Configuration.ExpectedFiles = expectedFiles;
		Configuration.Files.reserve(expectedFiles);
		Configuration.Begun = true;
		return 1;
	}

	EMSCRIPTEN_KEEPALIVE int Surreal_AddBrowserOPFSMountFile(const char* relativePath,
		uint32_t sizeLow, uint32_t sizeHigh)
	{
		if (!Configuration.Begun || Configuration.Mounted || !relativePath ||
			Configuration.Files.size() >= Configuration.ExpectedFiles ||
			!IsSafeRelativePath(relativePath))
			return 0;
		const std::string path = relativePath;
		if (std::any_of(Configuration.Files.begin(), Configuration.Files.end(),
			[&](const MountEntry& entry) { return entry.Path == path; }))
			return 0;
		Configuration.Files.push_back({ path,
			static_cast<uint64_t>(sizeLow) | (static_cast<uint64_t>(sizeHigh) << 32) });
		return 1;
	}
}

namespace BrowserGameDataMount
{
	bool MountConfigured()
	{
		if (!Configuration.Begun)
			return true;
		if (Configuration.Mounted)
			return true;
		if (Configuration.Files.size() != Configuration.ExpectedFiles)
			return Fail("OPFS mount manifest is incomplete");

		backend_t opfs = wasmfs_create_opfs_backend();
		if (wasmfs_create_directory(OPFSMountRoot, 0555, opfs) != 0)
			return Fail("could not mount the browser OPFS root");
		if (!EnsureDirectory(GameDataRoot, 0777))
			return Fail("could not create the game-data mount root");

		const std::string datasetRoot = std::string(OPFSMountRoot) + "/" +
			Configuration.StorageDirectory + "/imports/" + Configuration.DatasetId;
		std::set<std::string> sourceDirectories;
		uint64_t totalBytes = 0;
		uint32_t mutableBaselines = 0;
		for (const MountEntry& entry : Configuration.Files)
		{
			totalBytes += entry.Size;
			const std::string source = datasetRoot + "/" + entry.Path;
			const std::string destination = std::string(GameDataRoot) + "/" + entry.Path;
			struct stat sourceInfo = {};
			if (stat(source.c_str(), &sourceInfo) != 0 || !S_ISREG(sourceInfo.st_mode) ||
				static_cast<uint64_t>(sourceInfo.st_size) != entry.Size)
				return Fail("an OPFS game-data file is missing or has the wrong size: " + entry.Path);
			if (chmod(source.c_str(), 0444) != 0)
				return Fail("could not mark an OPFS game-data file read-only: " + entry.Path);
			if (!EnsureDirectory(ParentPath(destination), 0777))
				return Fail("could not create a game-data directory for: " + entry.Path);
			if (IsMutableGamePath(entry.Path))
			{
				if (!CopyMutableBaseline(source, destination))
					return Fail("could not create a mutable baseline for: " + entry.Path);
				mutableBaselines++;
			}
			else
			{
				if (PathExists(destination) || symlink(source.c_str(), destination.c_str()) != 0)
					return Fail("could not create a read-only game-data link for: " + entry.Path);
			}
			for (std::string directory = ParentPath(source); directory.size() >= datasetRoot.size();
				directory = ParentPath(directory))
			{
				sourceDirectories.insert(directory);
				if (directory == datasetRoot)
					break;
			}
		}
		for (auto it = sourceDirectories.rbegin(); it != sourceDirectories.rend(); ++it)
		{
			if (chmod(it->c_str(), 0555) != 0)
				return Fail("could not mark an OPFS game-data directory read-only");
		}
		Configuration.Mounted = true;
		printf("[data] mounted %u OPFS file(s) (%llu bytes); mutable-baselines=%u\n",
			Configuration.ExpectedFiles, static_cast<unsigned long long>(totalBytes), mutableBaselines);
		return true;
	}

	const std::string& LastError()
	{
		return Configuration.Error;
	}
}

extern "C" EMSCRIPTEN_KEEPALIVE int Surreal_PrepareBrowserOPFSMount()
{
	return BrowserGameDataMount::MountConfigured() ? 1 : 0;
}
