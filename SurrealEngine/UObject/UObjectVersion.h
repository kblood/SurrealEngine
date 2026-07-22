#pragma once

class UClass;

// UE1 package versions through 61 normally expose UObject's containing-object
// property as Parent. Bootstrap classes can retain the native Outer layout while
// Core.u is still loading, so the reflected class remains authoritative.
const char* GetUObjectOuterPropertyName(const UClass* cls, int packageVersion);
