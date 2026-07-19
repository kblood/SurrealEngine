#include "surrealwidgets/core/resourcedata.h"
#include <fstream>
#include <stdexcept>

// No real desktop font service under Emscripten (unlike the GTK/fontconfig
// path resourcedata_unix.cpp uses on Linux) - "system"/"monospace" named
// fonts (referenced by SurrealWidgets' default theme stylesheet) resolve to
// no font data here rather than failing hard, since M1 never draws
// SurrealWidgets UI (the Launcher is skipped) and CanvasFontGroup already
// tolerates an empty font list. See WEBXR_IMPLEMENTATION_PLAN.md M1.

static std::vector<uint8_t> ReadAllBytes(const std::string& filename)
{
	std::ifstream file(filename, std::ios::binary | std::ios::ate);
	if (!file)
		throw std::runtime_error("Could not open: " + filename);

	std::streamsize size = file.tellg();
	file.seekg(0, std::ios::beg);

	std::vector<uint8_t> buffer(size);
	if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
		throw std::runtime_error("Could not read: " + filename);

	return buffer;
}

std::vector<SingleFontData> ResourceData::LoadSystemFont()
{
	return {};
}

std::vector<SingleFontData> ResourceData::LoadMonospaceSystemFont()
{
	return {};
}

double ResourceData::GetSystemFontSize()
{
	return 11.0;
}

class ResourceLoaderEmscripten : public ResourceLoader
{
public:
	std::vector<SingleFontData> LoadFont(const std::string& name) override
	{
		if (name == "system")
			return ResourceData::LoadSystemFont();
		else if (name == "monospace")
			return ResourceData::LoadMonospaceSystemFont();
		else
			return { SingleFontData{ReadAllBytes(name + ".ttf"), ""} };
	}

	std::vector<uint8_t> ReadAllBytes(const std::string& filename) override
	{
		return ::ReadAllBytes(filename);
	}
};

struct ResourceDefaultLoader
{
	ResourceDefaultLoader() { loader = std::make_unique<ResourceLoaderEmscripten>(); }
	std::unique_ptr<ResourceLoader> loader;
};

static std::unique_ptr<ResourceLoader>& GetLoader()
{
	static ResourceDefaultLoader loader;
	return loader.loader;
}

ResourceLoader* ResourceLoader::Get()
{
	return GetLoader().get();
}

void ResourceLoader::Set(std::unique_ptr<ResourceLoader> instance)
{
	GetLoader() = std::move(instance);
}
