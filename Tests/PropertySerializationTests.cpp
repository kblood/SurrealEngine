#include "Precomp.h"
#include "Package/ObjectStream.h"
#include "Package/PackageStream.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/UStruct.h"
#include "Packages/Core/Properties/UArrayProperty.h"
#include "Packages/Core/Properties/UBoolProperty.h"
#include "Packages/Core/Properties/UFixedArrayProperty.h"
#include "Packages/Core/Properties/UIntProperty.h"
#include "Packages/Core/Properties/UObjectProperty.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Core/Properties/UStructProperty.h"
#include "UObject/UObjectVersion.h"
#include "Utils/File.h"

#include <chrono>
#include <filesystem>
#include <iostream>

int main()
{
	UObject arrayOwner("ArrayOwner", nullptr, ObjectFlags::NoFlags);
	UIntProperty arrayElement("Element", nullptr, ObjectFlags::NoFlags);
	ScriptArray arrayStorage(&arrayElement);
	arrayOwner.PropertyData.Data = &arrayStorage;
	arrayOwner.PropertyData.Size = sizeof(arrayStorage);
	auto arrayView = arrayOwner.DynamicArray<uint32_t>({ 0, 1 });
	if (arrayView.Array != &arrayStorage)
	{
		arrayOwner.PropertyData.Data = nullptr;
		arrayOwner.PropertyData.Size = 0;
		std::cerr << "FAILED: native dynamic-array access must wrap the inline ScriptArray storage\n";
		return 1;
	}
	arrayOwner.PropertyData.Data = nullptr;
	arrayOwner.PropertyData.Size = 0;

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

	UStruct dynamicString("DynamicString", nullptr, ObjectFlags::NoFlags);
	UStructProperty legacyString("LegacyString", nullptr, ObjectFlags::NoFlags);
	legacyString.SetStruct(&dynamicString, 61);
	UStructProperty modernStruct("ModernStruct", nullptr, ObjectFlags::NoFlags);
	modernStruct.SetStruct(&dynamicString, 62);
	if (!legacyString.UsesLegacyDynamicStringStorage() ||
		legacyString.ValueType != ExpressionValueType::ValueString ||
		legacyString.ElementSize() != sizeof(std::string) ||
		legacyString.ElementAlignment() != alignof(std::string) ||
		modernStruct.UsesLegacyDynamicStringStorage() ||
		modernStruct.ValueType != ExpressionValueType::ValueStruct)
	{
		std::cerr << "FAILED: v61 DynamicString must use dynamic-string storage only before StrProperty\n";
		return 1;
	}

	alignas(std::string) uint8_t legacyValue[sizeof(std::string)];
	alignas(std::string) uint8_t legacyCopy[sizeof(std::string)];
	legacyString.ConstructElement(legacyValue);
	legacyString.SetValueFromString(legacyValue, "travel-data");
	legacyString.CopyConstructElement(legacyCopy, legacyValue);
	if (!legacyString.CompareElement(legacyValue, legacyCopy) || legacyString.PrintValue(legacyCopy) != "\"travel-data\"")
	{
		legacyString.DestructElement(legacyCopy);
		legacyString.DestructElement(legacyValue);
		std::cerr << "FAILED: v61 DynamicString must preserve dynamic-string values\n";
		return 1;
	}
	legacyString.DestructElement(legacyCopy);
	legacyString.DestructElement(legacyValue);

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

			UIntProperty fixedInt("FixedInt", nullptr, ObjectFlags::NoFlags);
			fixedInt.ArrayDimension = 4;
			UStruct fixedIntStruct("FixedIntStruct", nullptr, ObjectFlags::NoFlags);
			fixedIntStruct.StructSize = sizeof(int32_t) * 4;
			fixedIntStruct.StructAlignment = alignof(int32_t);
			fixedIntStruct.Properties.push_back(&fixedInt);
			UStructProperty fixedIntStructProperty("FixedIntStructValue", nullptr, ObjectFlags::NoFlags);
			fixedIntStructProperty.Struct = &fixedIntStruct;
			int32_t fixedIntValues[4] = { 11, 22, 33, 44 };
			fixedIntStructProperty.SaveValue(fixedIntValues, &stream);

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
		const Array<uint8_t> expected = {
			0, 1, 0, 1, 1,
			11, 0, 0, 0, 22, 0, 0, 0, 33, 0, 0, 0, 44, 0, 0, 0,
			2, 0, 1
		};
		if (bytes != expected)
		{
			std::cerr << "FAILED: aggregate booleans must serialize as zero/one bytes\n";
			return 1;
		}

		UIntProperty fixedInt("FixedInt", nullptr, ObjectFlags::NoFlags);
		fixedInt.ArrayDimension = 4;
		UStruct fixedIntStruct("FixedIntStruct", nullptr, ObjectFlags::NoFlags);
		fixedIntStruct.StructSize = sizeof(int32_t) * 4;
		fixedIntStruct.StructAlignment = alignof(int32_t);
		fixedIntStruct.Properties.push_back(&fixedInt);
		UStructProperty fixedIntStructProperty("FixedIntStructValue", nullptr, ObjectFlags::NoFlags);
		fixedIntStructProperty.Struct = &fixedIntStruct;
		auto input = std::make_unique<uint64_t[]>(2);
		memcpy(input.get(), bytes.data() + 5, sizeof(int32_t) * 4);
		ObjectStream inputStream(nullptr, std::move(input), 0, sizeof(int32_t) * 4, NameString(), nullptr);
		int32_t loadedFixedInts[4] = {};
		fixedIntStructProperty.LoadStructMemberValue(loadedFixedInts, &inputStream);
		inputStream.ThrowIfNotEnd();
		if (loadedFixedInts[0] != 11 || loadedFixedInts[1] != 22 ||
			loadedFixedInts[2] != 33 || loadedFixedInts[3] != 44)
		{
			std::cerr << "FAILED: fixed-array struct members must round-trip every element\n";
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
