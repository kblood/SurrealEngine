#include "Audio/AudioStreamBufferQueue.h"
#include <array>
#include <cassert>
#include <cmath>

int main()
{
	AudioStreamBufferQueue<float*> queue;
	assert(queue.Resize(3));
	std::array<std::array<float, 8>, 3> generated{};
	for (size_t block = 0; block < generated.size(); block++)
	{
		for (size_t sample = 0; sample < generated[block].size(); sample++)
			generated[block][sample] = std::sin(float(block * generated[block].size() + sample) * 0.25f);
		assert(queue.Push(generated[block].data()));
	}
	assert(queue.Size() == 3 && !queue.Push(generated[0].data()));
	assert(queue.Pop() == generated[0].data());
	assert(queue.Pop() == generated[1].data());
	assert(queue.Push(generated[0].data())); // deterministic wraparound
	assert(queue.Pop() == generated[2].data());
	assert(queue.Pop() == generated[0].data());
	assert(queue.Empty());
	queue.Push(generated[1].data());
	assert(!queue.Resize(4)); // live buffers cannot be invalidated
	queue.Clear();
	assert(queue.Resize(4) && queue.Capacity() == 4);
	return 0;
}
