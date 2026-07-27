
#include "Precomp.h"
#include "NStatLogFile.h"
#include "VM/NativeFunc.h"
#include "Packages/Core/UObject.h"
#include "Engine.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>

// StatLogFile is the file writer UnrealScript uses for match statistics. Retail keeps the
// archive in the LogAr int property, which cannot hold a pointer on a 64 bit build, so the
// open streams are tracked here and keyed by object instead.
namespace
{
	std::map<UObject*, std::unique_ptr<std::ofstream>>& OpenStreams()
	{
		static std::map<UObject*, std::unique_ptr<std::ofstream>> streams;
		return streams;
	}

	std::ofstream* FindStream(UObject* self)
	{
		auto& streams = OpenStreams();
		auto it = streams.find(self);
		return it == streams.end() ? nullptr : it->second.get();
	}

	std::string PathProperty(UObject* self, const NameString& name)
	{
		if (!self || !self->HasProperty(name))
			return {};
		return self->GetString(name);
	}
}

void NStatLogFile::RegisterFunctions()
{
	RegisterVMNativeFunc_0("StatLogFile", "CloseLog", &NStatLogFile::CloseLog, 0);
	RegisterVMNativeFunc_0("StatLogFile", "FileFlush", &NStatLogFile::FileFlush, 0);
	RegisterVMNativeFunc_1("StatLogFile", "FileLog", &NStatLogFile::FileLog, 0);
	RegisterVMNativeFunc_1("StatLogFile", "GetChecksum", &NStatLogFile::GetChecksum, 0);
	RegisterVMNativeFunc_0("StatLogFile", "OpenLog", &NStatLogFile::OpenLog, 0);
	RegisterVMNativeFunc_1("StatLogFile", "Watermark", &NStatLogFile::Watermark, 0);
}

void NStatLogFile::CloseLog(UObject* Self)
{
	auto& streams = OpenStreams();
	auto it = streams.find(Self);
	if (it == streams.end())
		return;

	it->second->flush();
	it->second->close();
	streams.erase(it);

	// Retail renames the working file to StatLogFinal once the log is complete.
	const std::string working = PathProperty(Self, "StatLogFile");
	const std::string final = PathProperty(Self, "StatLogFinal");
	if (working.empty() || final.empty() || working == final)
		return;

	std::error_code error;
	std::filesystem::rename(working, final, error);
}

void NStatLogFile::FileFlush(UObject* Self)
{
	if (std::ofstream* stream = FindStream(Self))
		stream->flush();
}

void NStatLogFile::FileLog(UObject* Self, const std::string& EventString)
{
	if (std::ofstream* stream = FindStream(Self))
		(*stream) << EventString << '\n';
}

void NStatLogFile::GetChecksum(UObject* Self, std::string& Checksum)
{
	// Retail signs the log so a stats server can reject a tampered one. Nothing here
	// consumes that, so report a constant rather than inventing a signature.
	Checksum = "0";
}

void NStatLogFile::OpenLog(UObject* Self)
{
	const std::string path = PathProperty(Self, "StatLogFile");
	if (path.empty())
		return;

	std::error_code error;
	const std::filesystem::path target(path);
	if (target.has_parent_path())
		std::filesystem::create_directories(target.parent_path(), error);

	auto stream = std::make_unique<std::ofstream>(target, std::ios::out | std::ios::trunc);
	if (!stream->is_open())
		return;

	OpenStreams()[Self] = std::move(stream);
}

void NStatLogFile::Watermark(UObject* Self, const std::string& EventString)
{
	// Retail mixes the string into the checksum. With no checksum there is nothing to mix.
}
