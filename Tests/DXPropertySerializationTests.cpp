#include "Precomp.h"
#include "Package/PackageStream.h"
#include "UObject/UProperty.h"
#include "Utils/File.h"

#include <chrono>
#include <filesystem>
#include <iostream>

int main()
{
	const auto uniquePart = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path path = fs::temp_directory_path() / ("surreal-dx-property-" + std::to_string(uniquePart) + ".bin");

	try
	{
		UBoolProperty property("NestedBool", nullptr, ObjectFlags::NoFlags);
		uint32_t value = 0;
		{
			auto file = File::create_always(path.string());
			PackageStreamWriter stream(nullptr, file);

			// Tagged booleans store their value in the property header.
			property.SaveValue(&value, &stream);

			// Struct and array members have no tag header, so each value needs a byte.
			property.SetBool(&value, false);
			property.SaveStructMemberValue(&value, &stream);
			property.SetBool(&value, true);
			property.SaveStructMemberValue(&value, &stream);
		}

		const Array<uint8_t> bytes = File::read_all_bytes(path.string());
		File::try_delete(path.string());
		if (bytes.size() != 2 || bytes[0] != 0 || bytes[1] != 1)
		{
			std::cerr << "FAILED: nested booleans must serialize as zero/one bytes\n";
			return 1;
		}
	}
	catch (const std::exception& error)
	{
		File::try_delete(path.string());
		std::cerr << "FAILED: " << error.what() << '\n';
		return 1;
	}

	std::cout << "All Deus Ex property-serialization tests passed.\n";
	return 0;
}
