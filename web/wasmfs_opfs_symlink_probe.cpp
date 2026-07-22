#include <emscripten/heap.h>
#include <emscripten/wasmfs.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main()
{
	constexpr size_t chunkSize = 1024 * 1024;
	constexpr size_t fileSize = 32 * chunkSize;
	const size_t heapBefore = emscripten_get_heap_size();

	backend_t opfs = wasmfs_create_opfs_backend();
	assert(wasmfs_create_directory("/opfs", 0777, opfs) == 0);
	unlink("/gamedata/System/Core.u");
	rmdir("/gamedata/System");
	rmdir("/gamedata");
	unlink("/opfs/surreal-probe/System/Core.u");
	rmdir("/opfs/surreal-probe/System");
	rmdir("/opfs/surreal-probe");
	assert(mkdir("/opfs/surreal-probe", 0777) == 0);
	assert(mkdir("/opfs/surreal-probe/System", 0777) == 0);

	int output = open("/opfs/surreal-probe/System/Core.u", O_CREAT | O_TRUNC | O_WRONLY, 0666);
	assert(output >= 0);
	uint8_t* chunk = static_cast<uint8_t*>(malloc(chunkSize));
	assert(chunk);
	for (size_t index = 0; index < chunkSize; index++) chunk[index] = static_cast<uint8_t>(index);
	for (size_t offset = 0; offset < fileSize; offset += chunkSize) {
		assert(write(output, chunk, chunkSize) == static_cast<ssize_t>(chunkSize));
	}
	assert(close(output) == 0);
	free(chunk);

	assert(mkdir("/gamedata", 0777) == 0);
	assert(mkdir("/gamedata/System", 0777) == 0);
	assert(symlink("/opfs/surreal-probe/System/Core.u", "/gamedata/System/Core.u") == 0);

	struct stat info = {};
	assert(stat("/gamedata/System/Core.u", &info) == 0);
	assert(static_cast<size_t>(info.st_size) == fileSize);
	FILE* input = fopen("/gamedata/System/Core.u", "rb");
	assert(input);
	uint8_t sample[4096] = {};
	assert(fseek(input, static_cast<long>(fileSize - sizeof(sample)), SEEK_SET) == 0);
	assert(fread(sample, sizeof(sample), 1, input) == 1);
	assert(fclose(input) == 0);
	for (size_t index = 0; index < sizeof(sample); index++) {
		assert(sample[index] == static_cast<uint8_t>(index));
	}

	const size_t heapAfter = emscripten_get_heap_size();
	printf("PASS wasmfs-opfs-symlink file=%zu heap-before=%zu heap-after=%zu heap-growth=%zu\n",
		fileSize, heapBefore, heapAfter, heapAfter - heapBefore);

	assert(unlink("/gamedata/System/Core.u") == 0);
	assert(rmdir("/gamedata/System") == 0);
	assert(rmdir("/gamedata") == 0);
	assert(unlink("/opfs/surreal-probe/System/Core.u") == 0);
	assert(rmdir("/opfs/surreal-probe/System") == 0);
	assert(rmdir("/opfs/surreal-probe") == 0);
	return 0;
}
