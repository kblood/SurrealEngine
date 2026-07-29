#include "Precomp.h"
#include "UObjectVersion.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"

static bool HasProperty(const UClass* cls, const char* name)
{
	for (const UProperty* property : cls->Properties)
	{
		if (property->Name == name)
			return true;
	}
	return false;
}

const char* GetUObjectOuterPropertyName(const UClass* cls, int packageVersion)
{
	const char* expectedName = packageVersion <= 61 ? "Parent" : "Outer";
	const char* alternateName = packageVersion <= 61 ? "Outer" : "Parent";
	if (HasProperty(cls, expectedName))
		return expectedName;
	if (HasProperty(cls, alternateName))
		return alternateName;
	throw std::runtime_error("UObject class has neither an Outer nor Parent property");
}
