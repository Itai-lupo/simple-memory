#include "allocators/sharedMemoryPool.h"

#include "log.h"

#include "err.h"

#include "types/buddyAllocator.h"

#include "os/sharedMemoryFile.h"
#include "types/memoryAllocator.h"
#include "types/memoryMapInfo.h"

#include "allocators/dummyAllocator.h"
#include "allocators/unsafeAllocator.h"

#include <alloca.h>
#include <math.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>

// this 16GiB(2^34B) most system won't even by able to allocate that much ever so there will be errors out of memry most
// likly before we reach that
static constexpr const size_t MAX_RANGE_EXPONENT = 34;

// min block size is 16MiB to save on mangment size, this is the allocater that give blocks to the local pools there
// fore it dosn't need less then that
static constexpr const size_t MIN_BUDDY_BLOCK_SIZE_EXPONENT = 15;

static const off_t poolSize = sysconf(_SC_PAGESIZE) * 16;

static const memoryAllocator allocator = {&sharedAlloc, &sharedRealloc, &sharedDealloc, NULL};

const constexpr size_t freeListCount = GET_NEEDED_FREE_LISTS_COUNT(MAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT);

#define ALLOC_BUDDY_ON_STACK(list_size) (buddyAllocator *)alloca(sizeof(buddyAllocator) + list_size)

static buddyAllocator *g_buddy = nullptr;

#ifndef SLAB_ALLOCATION_CACHES_SIZES
#define SLAB_ALLOCATION_CACHES_SIZES                                                                                   \
	{                                                                                                                  \
		32, 64, 128, 255, 510, 1020, 2040, 4080, 8180                                                                  \
	}
#endif

const constexpr inline size_t allocationCachesSizes[] = SLAB_ALLOCATION_CACHES_SIZES;

static memoryAllocator tempCaches[256][sizeof(allocationCachesSizes) / sizeof(size_t)] = {
	{{NULL, NULL, NULL, NULL}}
};
/**
 * @brief in order to use the buddy allocator, we need a buddy allocator, so first we put it on the stack and then we
 * can copy it to somewhere else.
 *
 * @note we can't put this on that function stack as it will be freed at the end of the function so we get it from the
 * caller.
 */
THROWS static err_t initBuddyAllocatorOnStack(buddyAllocator *resBuddyAllocator,
											  void *buddyFreeListsData[freeListCount][100],
											  darray buddyFreeLists[freeListCount])
{
	err_t err = NO_ERRORCODE;

	resBuddyAllocator->memorySource = {nullptr, getSharedMemoryFileSize, setSharedMemoryFileSize};
	resBuddyAllocator->poolSizeExponent = MAX_RANGE_EXPONENT;
	resBuddyAllocator->smallestAllocationSizeExponent = MIN_BUDDY_BLOCK_SIZE_EXPONENT;
	resBuddyAllocator->freeListsCount = freeListCount;

	QUITE_RETHROW(getSharedMemoryFileStartAddr(&resBuddyAllocator->memorySource.startAddr));

	for (size_t i = 0; i < freeListCount; i++)
	{
		buddyFreeLists[i].data = buddyFreeListsData[i];
		resBuddyAllocator->freeLists[i] = buddyFreeLists + i;

		QUITE_RETHROW(darrayCreate(100, sizeof(void *), getDummyAllocator(), resBuddyAllocator->freeLists + i));
	}

	QUITE_RETHROW(initBuddyAllocator(resBuddyAllocator));

cleanup:
	return err;
}

/**
 * @brief after we have a real shared allocator we want to move all of the buddy allocator data to it from the stack
 */
THROWS static err_t moveBuddyFromStackToFinalAllocator(buddyAllocator **resBuddyAllocator, buddyAllocator *buddyOnStack)
{
	err_t err = NO_ERRORCODE;
	QUITE_RETHROW(buddyAlloc(buddyOnStack, (void **)resBuddyAllocator,
							 sizeof(buddyAllocator) +
								 sizeof(darray *) *
									 GET_NEEDED_FREE_LISTS_COUNT(MAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT)));

	memcpy(*resBuddyAllocator, buddyOnStack,
		   sizeof(buddyAllocator) +
			   sizeof(darray *) * GET_NEEDED_FREE_LISTS_COUNT(MAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT));

	for (size_t i = 0; i < GET_NEEDED_FREE_LISTS_COUNT(MAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT); i++)
	{
		QUITE_RETHROW(darraySwitchAllocator(&((*resBuddyAllocator)->freeLists[i]), &allocator));
	}

cleanup:
	return err;
}

/**
 * @brief we want each core to alloc from a memory that is garnted to be thread safe
 * so each cpu core can only allocate from it own buffer and there is a process that fill them up
 * @note thank you to tcmalloc for the idea.
 */
THROWS err_t initCoreCaches(buddyAllocator *buddyOnStack)
{
	err_t err = NO_ERRORCODE;
	long coreCount = sysconf(_SC_NPROCESSORS_ONLN);
	uint32_t coreId = 0;
	slab *tempSlab;
	QUITE_CHECK(buddyOnStack != nullptr);

	getcpu(&coreId, NULL);

	for (int i = 0; i < coreCount; i++)
	{
		for (size_t j = 0; j < sizeof(allocationCachesSizes) / sizeof(size_t); j++)
		{
			QUITE_RETHROW(buddyAlloc(buddyOnStack, (void**)&tempSlab, SLAB_SIZE));
			QUITE_RETHROW(createUnsafeAllocator(&tempCaches[i][j], tempSlab, allocationCachesSizes[j]));
		}
	}

cleanup:
	return err;
}

THROWS err_t initSharedMemory()
{
	err_t err = NO_ERRORCODE;

	buddyAllocator *buddy = (buddyAllocator *)alloca(sizeof(buddyAllocator) + sizeof(buddyAllocator) * freeListCount);
	darray buddyFreeLists[freeListCount];
	void *buddyFreeListsData[freeListCount][100] = {{nullptr}};

	CHECK(g_buddy == nullptr);

	QUITE_RETHROW(initSharedMemoryFile(pow(2, MAX_RANGE_EXPONENT)));

	QUITE_RETHROW(initBuddyAllocatorOnStack(buddy, buddyFreeListsData, buddyFreeLists));

	QUITE_RETHROW(initCoreCaches(buddy));

	QUITE_RETHROW(moveBuddyFromStackToFinalAllocator(&g_buddy, buddy));

cleanup:
	return err;
}

err_t closeSharedMemory()
{
	err_t err = NO_ERRORCODE;

	QUITE_RETHROW(closeBuddyAllocator(g_buddy));
	g_buddy = nullptr;

cleanup:
	REWARN(closeSharedMemoryFile());
	return err;
}

THROWS err_t sharedAlloc(void **const data, const size_t count, const size_t size, allocatorFlags flags, [[maybe_unused]] void *sharedAllocatorData)
{
	err_t err = NO_ERRORCODE;
	uint32_t coreId = 0;
	uint32_t sizeClass = UINT32_MAX;

	QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data == NULL);
	QUITE_CHECK(size > 0);

	getcpu(&coreId, NULL);
  
  for(size_t i = 0; i < sizeof(allocationCachesSizes) / sizeof(size_t); i++)
  {
    if(count * size < allocationCachesSizes[i])
    {
      sizeClass = i;
      break;
    }
  }

  QUITE_CHECK(sizeClass != UINT32_MAX)

	QUITE_RETHROW(tempCaches[coreId][sizeClass].alloc(data, count, size, flags, tempCaches[coreId][sizeClass].data));

	if ((flags | ALLOCATOR_CLEAR_MEMORY) == 1)
	{
		memset(*data, 0, count * size);
	}

cleanup:
	return err;
}

THROWS err_t sharedRealloc(void **const data, const size_t count, const size_t size, allocatorFlags flags, [[maybe_unused]] void *sharedAllocatorData)
{
	err_t err = NO_ERRORCODE;
	uint32_t coreId = 0;

	QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data != NULL);
	QUITE_CHECK(size > 0);

	getcpu(&coreId, NULL);


	if (flags || ALLOCATOR_CLEAR_MEMORY == 1)
	{
		memset(*data, 0, count * size);
	}

cleanup:
	return err;
}

THROWS err_t sharedDealloc(void **const data, [[maybe_unused]] void *sharedAllocatorData)
{
	err_t err = NO_ERRORCODE;

	QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data != NULL);


cleanup:
	if (data != NULL)
	{
		*data = NULL;
	}

	return err;
}
