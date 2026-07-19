
#include "Precomp.h"
#include "SHA1Sum.h"
#include "TinySHA1/TinySHA1.hpp"

std::string SHA1Sum::of_file(const fs::path& filepath)
{
	// Open once and reuse the handle - a second, separate open_existing() call
	// (as opposed to reading from this same handle) is a TOCTOU race: nothing
	// guarantees the second open succeeds just because the first one did.
	auto file = File::try_open_existing(filepath.string());
	if (!file)
		return "";

	Array<uint8_t> bytes(file->size());
	file->read(bytes.data(), bytes.size());

	sha1::SHA1 s;
	s.processBytes(bytes.data(), bytes.size());
	uint32_t digest[5];
	s.getDigest(digest);

	char temp[41];
	snprintf(temp, 41, "%08x%08x%08x%08x%08x", digest[0], digest[1], digest[2], digest[3], digest[4]);

	return std::string{temp};
}
