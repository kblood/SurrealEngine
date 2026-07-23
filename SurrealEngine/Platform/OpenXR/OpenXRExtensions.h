#pragma once

#include <cstddef>
#include <string>
#include <vector>

// OpenXR's Vulkan extension queries return a space-delimited, NUL-terminated
// byte buffer whose reported size includes the terminator. Stop at the first
// NUL so neither the terminator nor bytes beyond it can become an extension.
std::vector<std::string> ParseOpenXRExtensionList(const char* data, size_t size);
