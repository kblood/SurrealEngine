#include "Precomp.h"
#include "ClipSpaceConversion.h"

std::array<float, 16> ConvertProjectionDepthMinusOneToOneToZeroToOne(const std::array<float, 16>& projection)
{
	auto converted = projection;

	// z' = 0.5 * z + 0.5 * w. In a column-major matrix this replaces row 2
	// with half of row 2 plus half of row 3 while preserving asymmetric X/Y.
	for (size_t column = 0; column < 4; column++)
	{
		const size_t z = column * 4 + 2;
		const size_t w = column * 4 + 3;
		converted[z] = 0.5f * projection[z] + 0.5f * projection[w];
	}

	return converted;
}
