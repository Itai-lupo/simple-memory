#pragma once

#include "types/dynamicArray.h"
#include "types/err_t.h"
#include "types/memoryAllocator.h"

#ifndef SLAB_SIZE
#define SLAB_SIZE 32764
#endif

#ifndef SLAB_MAGIC
#define SLAB_MAGIC 0xABABABABABABABAB
#endif

extern bool isInRseq;

struct slab;

typedef struct {
	uint64_t slabMagic;
	slab *nextSlab;
	size_t cellSize;
	bool isSlabFull;
} slabHead;

static const constexpr size_t SLAB_CACHE_SIZE = (SLAB_SIZE - sizeof(slabHead));

typedef struct slab
{
	slabHead header;
	uint8_t cache[SLAB_CACHE_SIZE];
} slab;

#ifdef __cplusplus
extern "C"
{
#endif


	/**
	 * @brief create a allocator that can be called from a resq or a critical section
	 * @see man resq(2)
	 * @return memoryAllocator*
	 */
	THROWS err_t createUnsafeAllocator(memoryAllocator *res, slab *firstSlab, size_t cellSize);
	THROWS err_t appendSlab(memoryAllocator *unsafeAllocator, slab *newSlab);

#ifdef __cplusplus
}
#endif
