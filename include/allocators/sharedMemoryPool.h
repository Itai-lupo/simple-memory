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

	THROWS err_t sharedAlloc(void **const data, const size_t count, const size_t size, allocatorFlags flags);
	THROWS err_t sharedRealloc(void **const data, const size_t count, const size_t size, allocatorFlags flags);
	THROWS err_t sharedDealloc(void **const data);

	/*  void *malloc(size_t size);
	 void free(void *_Nullable ptr);
	 void *calloc(size_t nmemb, size_t size);
	 void *realloc(void *_Nullable ptr, size_t size);
	 void *reallocarray(void *_Nullable ptr, size_t nmemb, size_t size);

  */

#ifdef __cplusplus
}
#endif
