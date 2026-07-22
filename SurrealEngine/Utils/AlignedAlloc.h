#pragma once

#include <cstdlib>

inline void* AlignedAlloc(std::size_t alignment, std::size_t size)
{
#ifdef _MSC_VER
	return _aligned_malloc(size, alignment);
#else
	// std::aligned_alloc requires alignment to be a power of two supported by
	// the implementation - musl (and glibc) reject alignment < sizeof(void*)
	// (returns null rather than rounding up), even though callers here can
	// legitimately compute an alignment of 1 for byte-only structs. Clamp to
	// the smallest alignment the allocator actually accepts.
	if (alignment < sizeof(void*))
		alignment = sizeof(void*);
	// std::aligned_alloc requires size to be an integral multiple of alignment
	// (UB otherwise) - callers pass struct sizes that aren't necessarily
	// alignment-padded, so round up here.
	size = (size + alignment - 1) / alignment * alignment;
	return std::aligned_alloc(alignment, size);
#endif
}

inline void AlignedFree(void* data)
{
#ifdef _MSC_VER
	_aligned_free(data);
#else
	std::free(data);
#endif
}
