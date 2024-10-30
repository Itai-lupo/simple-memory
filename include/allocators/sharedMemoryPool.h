#pragma once
#include "types/err_t.h"
#include <cstddef>
#include <unistd.h>

#include "types/memoryAllocator.h"
#ifdef __cplusplus
extern "C"
{
#endif

	THROWS err_t initSharedMemory();
	err_t closeSharedMemory();

	THROWS err_t sharedAlloc(void **const data, const size_t count, const size_t size, allocatorFlags flags, void *sharedAllocatorData);
	THROWS err_t sharedRealloc(void **const data, const size_t count, const size_t size, allocatorFlags flags, void *sharedAllocatorData);
	THROWS err_t sharedDealloc(void **const data,  void *sharedAllocatorData);

	/**
	 * @brief get the shared Allocator.
	 * this in a way is singleton as it will always will return the same shared allocator
	 *
	 * @return memoryAllocator*
	 */
	memoryAllocator *getSharedAllocator();

  

#ifdef __cplusplus
}
#endif
