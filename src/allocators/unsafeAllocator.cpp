#include "allocators/unsafeAllocator.h"

#include "defaultTrace.h"
#include "defines/checkApi.h"
#include "os/rseq.h"

#include "err.h"

#include <stdatomic.h>
#include <stdint.h>
#include <strings.h>

USED_IN_RSEQ
static constexpr long ceil2(double x)
{
	if ((int64_t)x > INT64_MAX)
		return (int64_t)x; // big floats are all ints
	return ((long)(x + (0.99999999999999997)));
}

USED_IN_RSEQ
static constexpr size_t GET_SLAB_FREE_LIST_SIZE(size_t cellSize)
{
	return ((size_t)ceil2((float)((SLAB_SIZE - sizeof(slabHead)) / cellSize) / 8.0f));
}

USED_IN_RSEQ
static constexpr size_t GET_SLAB_CELL_INDEX(size_t cellSize, off_t i)
{
	return (GET_SLAB_FREE_LIST_SIZE(cellSize) + 1 + (i)*cellSize);
}

USED_IN_RSEQ
static constexpr size_t IS_CELL_IN_SLAB(size_t cellSize, off_t i)
{
	return (GET_SLAB_CELL_INDEX(cellSize, i) <= SLAB_CACHE_SIZE);
}

USED_IN_RSEQ
static constexpr int8_t findFirstZeroInByte(uint8_t byte)
{
	for (int8_t i = 0; i < 8; i++)
	{
		if ((byte & (1 << i)) == 0)
		{
			return i;
		}
	}
	return -1;
}

USED_IN_RSEQ
static constexpr int findFirstZeroInByteArray(uint8_t *byteArray, size_t byteArraySize)
{
	int8_t emptyBitIndex = -1;

	for (size_t i = 0; i < byteArraySize; i++)
	{
		if (byteArray[i] != 255)
		{
			emptyBitIndex = findFirstZeroInByte(byteArray[i]);
		}

		if (emptyBitIndex != -1)
		{
			return emptyBitIndex + i * 8;
		}
	}

	return -1;
}

USED_IN_RSEQ THROWS static err_t unsafeAlloc(void **const ptr, const size_t count, const size_t size,
											 [[maybe_unused]] allocatorFlags flags, void *firstSlab)
{
	err_t err = NO_ERRORCODE;
	slab *slabContent = (slab *)firstSlab;
	slab *currentSlab = slabContent;
	int freeIndex = -1;
	size_t freeListSize = 0;
	int i = 0;

	QUITE_CHECK(ptr != NULL)
	QUITE_CHECK(count > 0 && size > 0);
	QUITE_CHECK(*ptr == NULL);
	QUITE_CHECK(firstSlab != NULL);
	QUITE_CHECK(slabContent->header.slabMagic == SLAB_MAGIC);
	QUITE_CHECK(slabContent->header.cellSize >= size * count);
	freeListSize = GET_SLAB_FREE_LIST_SIZE(slabContent->header.cellSize);

	do
	{
		QUITE_CHECK(i < 1000000);
		if (currentSlab->header.isSlabFull == false)
		{
			freeIndex = findFirstZeroInByteArray(currentSlab->cache, freeListSize);
			if (freeIndex != -1 && IS_CELL_IN_SLAB(currentSlab->header.cellSize, freeIndex + 1))
			{
				currentSlab->cache[freeIndex / 8] |= (1 << (freeIndex % 8));
				*ptr = (void *)&currentSlab->cache[GET_SLAB_CELL_INDEX(currentSlab->header.cellSize, freeIndex)];

				QUITE_CHECK((size_t)*ptr + slabContent->header.cellSize < (size_t)currentSlab + SLAB_SIZE);
			}
			else
			{
				currentSlab->header.isSlabFull = true;
			}
		}
		i += 1;
	} while (*ptr == NULL && (currentSlab = currentSlab->header.nextSlab) != NULL);

	CHECK_NOTRACE_ERRORCODE(*ptr != NULL, ENOMEM);
	QUITE_CHECK((size_t)*ptr + slabContent->header.cellSize < (size_t)currentSlab + SLAB_SIZE);
	QUITE_CHECK((size_t)*ptr > (size_t)&currentSlab->cache[freeListSize]);

cleanup:
	return err;
}

THROWS err_t unsafeRealloc(void **const ptr, const size_t count, const size_t size,
						   [[maybe_unused]] allocatorFlags flags, void *firstSlab)
{
	err_t err = NO_ERRORCODE;
	slab *slabContent = (slab *)firstSlab;

	QUITE_CHECK(ptr != NULL);
	QUITE_CHECK(*ptr != NULL);
	QUITE_CHECK(count > 0 && size > 0);
	QUITE_CHECK(firstSlab != NULL);
	QUITE_CHECK(slabContent->header.slabMagic == SLAB_MAGIC);

	CHECK_NOTRACE_ERRORCODE(slabContent->header.cellSize >= count * size, E2BIG);

cleanup:
	return err;
}

THROWS err_t unsafeDealloc(void **const ptr, void *data)
{
	err_t err = NO_ERRORCODE;
	size_t cellOffset = 0;
	size_t cellIndex = 0;

	volatile slab *s = (slab *)data;

	QUITE_CHECK(ptr != NULL);
	QUITE_CHECK(*ptr != NULL);
	QUITE_CHECK(s != NULL);

	QUITE_CHECK(s->header.slabMagic == SLAB_MAGIC);
	QUITE_CHECK(s->header.cellSize > 0);

	QUITE_CHECK((size_t)*ptr >= (size_t)&s->cache[GET_SLAB_CELL_INDEX(s->header.cellSize, 0)])
	QUITE_CHECK((size_t)*ptr < (size_t)&s->cache[SLAB_CACHE_SIZE])

	cellOffset = ((size_t)*ptr - (size_t)&s->cache[GET_SLAB_CELL_INDEX(s->header.cellSize, 0)]);
	QUITE_CHECK(cellOffset % s->header.cellSize == 0);
	QUITE_CHECK(cellOffset <= SLAB_CACHE_SIZE)
	cellIndex = cellOffset / s->header.cellSize;

	QUITE_CHECK(&s->cache[GET_SLAB_CELL_INDEX(s->header.cellSize, cellIndex)] == *ptr);
	QUITE_CHECK((s->cache[cellIndex] | (1 << (cellIndex % 8))) > 0);

	s->cache[cellIndex / 8] &= ~(1 << (cellIndex % 8));
	s->header.isSlabFull = false;

	*ptr = NULL;

cleanup:
	return err;
}

err_t createUnsafeAllocator(memoryAllocator *res, slab *firstSlab, size_t cellSize)
{
	err_t err = NO_ERRORCODE;
	size_t freeListSize = 0;

	QUITE_CHECK(res != nullptr);
	QUITE_CHECK(firstSlab != nullptr)

	res->alloc = unsafeAlloc;
	res->realloc = unsafeRealloc;
	res->free = unsafeDealloc;
	res->data = firstSlab;

	firstSlab->header.nextSlab = nullptr;
	firstSlab->header.slabMagic = SLAB_MAGIC;
	firstSlab->header.cellSize = cellSize;

	freeListSize = GET_SLAB_FREE_LIST_SIZE(cellSize);

	for (size_t i = 0; i < freeListSize; i++)
	{
		firstSlab->cache[i] = 0;
	}

cleanup:
	return err;
}

err_t appendSlab(memoryAllocator *unsafeAllocator, slab *newSlab)
{
	err_t err = NO_ERRORCODE;

	size_t freeListSize = 0;
	slab *firstSlab = NULL;

	QUITE_CHECK(unsafeAllocator != NULL);
	QUITE_CHECK(newSlab != NULL);

	firstSlab = (slab *)unsafeAllocator->data;

	QUITE_CHECK(firstSlab != NULL);

	newSlab->header.nextSlab = nullptr;
	newSlab->header.slabMagic = SLAB_MAGIC;
	newSlab->header.cellSize = firstSlab->header.cellSize;

	freeListSize = GET_SLAB_FREE_LIST_SIZE(firstSlab->header.cellSize);
	bzero(newSlab->cache, freeListSize);

	do
	{
		newSlab->header.nextSlab = (slab *)unsafeAllocator->data;
	} while (!atomic_compare_exchange_weak((void *_Atomic *)&unsafeAllocator->data, (void **)&firstSlab, newSlab));

cleanup:
	return err;
}
