#pragma once

#include <cstddef>
#include <cstdlib>
#include <type_traits>

// Allocation-free-after-resize FIFO used by both threaded native music and
// main-thread browser music. The caller owns pointed-to sample buffers.
template<typename T>
class AudioStreamBufferQueue
{
public:
	static_assert(std::is_trivially_copyable_v<T>);
	AudioStreamBufferQueue() = default;
	~AudioStreamBufferQueue() { std::free(data); }
	AudioStreamBufferQueue(const AudioStreamBufferQueue&) = delete;
	AudioStreamBufferQueue& operator=(const AudioStreamBufferQueue&) = delete;

	bool Resize(size_t newSize)
	{
		if (count != 0) return false;
		if (newSize == 0) { std::free(data); data = nullptr; capacity = current = 0; return true; }
		T* resized = static_cast<T*>(std::realloc(data, sizeof(T) * newSize));
		if (!resized) return false;
		data = resized; capacity = newSize; current = 0; return true;
	}
	bool Empty() const { return count == 0; }
	size_t Size() const { return count; }
	size_t Capacity() const { return capacity; }
	T& Front() { return data[current]; }
	T& GetNextFree() { return data[(current + count) % capacity]; }
	bool Push(const T& value)
	{
		if (count == capacity) return false;
		GetNextFree() = value; count++; return true;
	}
	T& Pop()
	{
		T& result = data[current];
		if (count) { current = (current + 1) % capacity; count--; }
		return result;
	}
	void Clear() { current = 0; count = 0; }

private:
	T* data = nullptr;
	size_t capacity = 0;
	size_t current = 0;
	size_t count = 0;
};
