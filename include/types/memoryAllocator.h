#pragma once

#include "types/err_t.h"

#define ALLOCATOR_CLEAR_MEMORY 0x1

typedef int allocatorFlags;

typedef err_t (*allocFuncion)(void **const ptr, const size_t count, const size_t size, allocatorFlags flags,
							  void *data);
typedef err_t (*reallocFuncion)(void **const ptr, const size_t count, const size_t size, allocatorFlags flags,
								void *data);
typedef err_t (*deallocFuncion)(void **const ptr, void *data);

typedef struct
{
	THROWS allocFuncion alloc;
	THROWS reallocFuncion realloc;
	THROWS deallocFuncion free;
	void *data;
} memoryAllocator;
