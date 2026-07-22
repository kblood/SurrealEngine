#include "Precomp.h"
#include "Package/PackageStream.h"
#include "UObject/UProperty.h"
#include "UObject/UObjectVersion.h"
#include "Utils/File.h"

#include <chrono>
#include <filesystem>
#include <iostream>

int main()
{
	UClass oldObjectClass("OldObject", nullptr, ObjectFlags::NoFlags);
	UObjectProperty parentProperty("Parent", nullptr, ObjectFlags::NoFlags);
	oldObjectClass.Properties.push_back(&parentProperty);

	UClass modernObjectClass("ModernObject", nullptr, ObjectFlags::NoFlags);
	UObjectProperty outerProperty("Outer", nullptr, ObjectFlags::NoFlags);
	modernObjectClass.Properties.push_back(&outerProperty);

	if (std::string(GetUObjectOuterPropertyName(&oldObjectClass, 61)) != "Parent" ||
		std::string(GetUObjectOuterPropertyName(&modernObjectClass, 62)) != "Outer" ||
		std::string(GetUObjectOuterPropertyName(&modernObjectClass, 61)) != "Outer")
	{
		std::cerr << "FAILED: UObject container-property name must follow the reflected UE1 layout\n";
		return 1;
	}

	const auto uniquePart = std::chrono::steady_clock::now().time_since_epoch().count();
	const fs::path path = fs::temp_directory_path() / ("surreal-property-" + std::to_string(uniquePart) + ".bin");

	try
	{
		UBoolProperty property("NestedBool", nullptr, ObjectFlags::NoFlags);
		{
			auto file = File::create_always(path.string());
			PackageStreamWriter stream(nullptr, file);

			// Tagged booleans store their value in the property header.
			uint32_t value = 0;
			property.SaveValue(&value, &stream);

			// Untagged values use their representation from the aggregate payload.
			property.SetBool(&value, false);
			property.SaveStructMemberValue(&value, &stream);
			property.SetBool(&value, true);
			property.SaveStructMemberValue(&value, &stream);

			uint32_t fixedValues[2] = {};
			property.SetBool(&fixedValues[1], true);
			UFixedArrayProperty fixedArray("FixedBools", nullptr, ObjectFlags::NoFlags);
			fixedArray.Inner = &property;
			fixedArray.Count = 2;
			fixedArray.SaveValue(fixedValues, &stream);

			UStruct structType("BoolStruct", nullptr, ObjectFlags::NoFlags);
			structType.StructSize = sizeof(uint32_t);
			structType.StructAlignment = alignof(uint32_t);
			structType.Properties.push_back(&property);
			UStructProperty structProperty("StructValue", nullptr, ObjectFlags::NoFlags);
			structProperty.Struct = &structType;
			uint32_t structValue = 0;
			property.SetBool(&structValue, true);
			structProperty.SaveValue(&structValue, &stream);

			UArrayProperty arrayProperty("BoolArray", nullptr, ObjectFlags::NoFlags);
			arrayProperty.Inner = &property;
			ScriptArray arrayValues(&property);
			arrayValues.Resize(2);
			property.SetBool(arrayValues.GetItem(0), false);
			property.SetBool(arrayValues.GetItem(1), true);
			arrayProperty.SaveValue(&arrayValues, &stream);
		}

		const Array<uint8_t> bytes = File::read_all_bytes(path.string());
		File::try_delete(path.string());
		const Array<uint8_t> expected = { 0, 1, 0, 1, 1, 2, 0, 1 };
		if (bytes != expected)
		{
			std::cerr << "FAILED: aggregate booleans must serialize as zero/one bytes\n";
			return 1;
		}
	}
	catch (const std::exception& error)
	{
		File::try_delete(path.string());
		std::cerr << "FAILED: " << error.what() << '\n';
		return 1;
	}

	std::cout << "All property-serialization tests passed.\n";
	return 0;
}
