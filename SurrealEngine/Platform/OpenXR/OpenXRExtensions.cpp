#include "OpenXRExtensions.h"

#include <cctype>

std::vector<std::string> ParseOpenXRExtensionList(const char* data, size_t size)
{
	std::vector<std::string> result;
	if (!data || size == 0)
		return result;

	size_t contentSize = 0;
	while (contentSize < size && data[contentSize] != '\0')
		contentSize++;

	size_t offset = 0;
	while (offset < contentSize)
	{
		while (offset < contentSize && std::isspace(static_cast<unsigned char>(data[offset])))
			offset++;
		if (offset == contentSize)
			break;
		const size_t start = offset;
		while (offset < contentSize && !std::isspace(static_cast<unsigned char>(data[offset])))
			offset++;
		result.emplace_back(data + start, offset - start);
	}
	return result;
}
