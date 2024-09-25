#include "allocators/sharedMemoryPool.h"

#include "log.h"

#include "err.h"

#include "types/buddyAllocator.h"

#include "os/sharedMemoryFile.h"
#include "types/memoryAllocator.h"
#include "types/memoryMapInfo.h"

#include "allocators/dummyAllocator.h"
#include "allocators/unsafeAllocator.h"

#include "memoryUtils/allocatorsConsts.h"
#include "memoryUtils/allocatorsUtilFunctions.h"

#include <alloca.h>
#include <math.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>


static const memoryAllocator allocator = {&sharedAlloc, &sharedRealloc, &sharedDealloc, NULL};

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

	QUITE_RETHROW(moveBuddyFromStackToFinalAllocator(&g_buddy, buddy, &allocator));

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


THROWS static err_t handleSlabAlloc(void **const data, uint32_t sizeClass, allocatorFlags flags)
{
	err_t err = NO_ERRORCODE;
	slab *tempSlab;
	uint32_t coreId = 0;

  getcpu(&coreId, NULL);
	err = tempCaches[coreId][sizeClass].alloc(data, 1, allocationCachesSizes[sizeClass], flags, tempCaches[coreId][sizeClass].data);
  if(err.errorCode == ENOMEM)
  {
    err = NO_ERRORCODE;

    // if we the cache has no more memory we might be able to just add more memory to it and try agin.
    // but we only need to check on the new memory we added.
		QUITE_RETHROW(buddyAlloc(g_buddy, (void**)&tempSlab, SLAB_SIZE));
    QUITE_RETHROW(appendSlab(&tempCaches[coreId][sizeClass], tempSlab));
	  QUITE_RETHROW(tempCaches[coreId][sizeClass].alloc(data, 1,  allocationCachesSizes[sizeClass], flags, (void*)tempSlab));
  } 
  else
  {
    QUITE_RETHROW(err);
  }

cleanup:
  return err;
}

THROWS err_t sharedAlloc(void **const data, const size_t count, const size_t size, allocatorFlags flags, [[maybe_unused]] void *sharedAllocatorData)
{
	err_t err = NO_ERRORCODE;
	uint32_t sizeClass = UINT32_MAX;

	QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data == NULL);
	QUITE_CHECK(size > 0);
  
  sizeClass = getSizeClass(size * count);

  if(sizeClass == UINT32_MAX)
  {
    QUITE_RETHROW(buddyAlloc(g_buddy, data, count * size));
  } 
  else
  {
    QUITE_RETHROW(handleSlabAlloc(data, sizeClass, flags));
  }

	if ((flags | ALLOCATOR_CLEAR_MEMORY) == 1)
	{
		bzero(*data, count * size);
	}

cleanup:
	return err;
}

THROWS err_t sharedRealloc(void **const data, const size_t count, const size_t size, allocatorFlags flags, [[maybe_unused]] void *sharedAllocatorData)
{
	err_t err = NO_ERRORCODE;
  slab *s = NULL;
  void *temp = NULL;
	
  QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data != NULL);
	QUITE_CHECK(size > 0);

  temp = *data;
  s = GET_SLAB_START(*data);

  if(s->header.slabMagic == SLAB_MAGIC)
  {
    err = tempCaches[0][0].realloc(data, count, size, flags, s);
    if(err.errorCode == ENOMEM)
    {
      err = NO_ERRORCODE;
      QUITE_RETHROW(sharedAlloc(data, count, size, flags, sharedAllocatorData));
      memcpy(*data, temp, MIN(size * count, s->header.cellSize));
      QUITE_RETHROW(tempCaches[0][0].free(&temp, s));
    }
    else
    {
      QUITE_RETHROW(err);
    }
  }
  else
  {
    QUITE_RETHROW(buddyFree(g_buddy, data));
    QUITE_RETHROW(buddyAlloc(g_buddy, data, count * size));
  }

  
cleanup:
	return err;
}



THROWS err_t sharedDealloc(void **const data, [[maybe_unused]] void *sharedAllocatorData)
{
	err_t err = NO_ERRORCODE;
  slab *s= NULL;
	
  QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data != NULL);
  
  s = GET_SLAB_START(*data);

  if(s->header.slabMagic == SLAB_MAGIC)
  {
    QUITE_RETHROW(tempCaches[0][0].free(data, s));
  }
  else
  {
    QUITE_RETHROW(buddyFree(g_buddy, data));
  }
  
cleanup:
	if (data != NULL)
	{
		*data = NULL;
	}

	return err;
}
