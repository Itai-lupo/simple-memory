#include "allocators/sharedMemoryPool.h"

#include "log.h"

#include "err.h"

#include "types/buddyAllocator.h"

#include "os/sharedMemoryFile.h"
#include "types/memoryAllocator.h"
#include "types/memoryMapInfo.h"

#include "allocators/dummyAllocator.h"

#include <alloca.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>

// this 16GiB(2^34B) most system won't even by able to allocate that much ever so there will be errors out of memry most
// likly before we reach that
static const size_t NAX_RANGE_EXPONENT = 34;

// min block size is 16MiB to save on mangment size, this is the allocater that give blocks to the local pools there
// fore it dosn't need less then that
static const size_t MIN_BUDDY_BLOCK_SIZE_EXPONENT = 21;

static const off_t poolSize = sysconf(_SC_PAGESIZE) * 16;

static memoryAllocator allocator = {&sharedAlloc, &sharedRealloc, &sharedDealloc};

buddyAllocator *g_buddy = nullptr;

THROWS err_t initSharedMemory()
{
	err_t err = NO_ERRORCODE;

	void *buddyFreeListsData[GET_NEEDED_FREE_LISTS_COUNT(NAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT)][100] = {
		{NULL}};
	darray buddyFreeLists[GET_NEEDED_FREE_LISTS_COUNT(NAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT)];

	buddyAllocator *buddy = (buddyAllocator *)alloca(
		sizeof(buddyAllocator) +
		sizeof(darray *) * GET_NEEDED_FREE_LISTS_COUNT(NAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT));

	CHECK(g_buddy == nullptr);

	buddy->memorySource = {nullptr, getSharedMemoryFileSize, setSharedMemoryFileSize};
	buddy->poolSizeExponent = NAX_RANGE_EXPONENT;
	buddy->smallestAllocationSizeExponent = MIN_BUDDY_BLOCK_SIZE_EXPONENT;
	buddy->freeListsCount = GET_NEEDED_FREE_LISTS_COUNT(NAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT);

	for (size_t i = 0; i < GET_NEEDED_FREE_LISTS_COUNT(NAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT); i++)
	{
		buddyFreeLists[i].data = (void *)buddyFreeListsData[i];
		buddy->freeLists[i] = buddyFreeLists + i;

		QUITE_RETHROW(darrayCreate(100, sizeof(void *), getDummyAllocator(), buddy->freeLists + i));
	}

	QUITE_RETHROW(initSharedMemoryFile(pow(2, NAX_RANGE_EXPONENT)));
	QUITE_RETHROW(getSharedMemoryFileStartAddr(&buddy->memorySource.startAddr));

	QUITE_RETHROW(initBuddyAllocator(buddy));

	QUITE_RETHROW(buddyAlloc(buddy, (void **)&g_buddy,
							 sizeof(buddyAllocator) +
								 sizeof(darray *) *
									 GET_NEEDED_FREE_LISTS_COUNT(NAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT)));
	memcpy(g_buddy, buddy,
		   sizeof(buddyAllocator) +
			   sizeof(darray *) * GET_NEEDED_FREE_LISTS_COUNT(NAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT));

	for (size_t i = 0; i < GET_NEEDED_FREE_LISTS_COUNT(NAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT); i++)
	{
		QUITE_RETHROW(darraySwitchAllocator(&g_buddy->freeLists[i], &allocator));
	}

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

THROWS err_t sharedAlloc(void **const data, const size_t count, const size_t size, allocatorFlags flags)
{
	err_t err = NO_ERRORCODE;

	QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data == NULL);
	QUITE_CHECK(size > 0);

	QUITE_RETHROW(buddyAlloc(g_buddy, data, size * count));

	if (flags || ALLOCATOR_CLEAR_MEMORY == 1)
	{
		memset(*data, 0, count * size);
	}

cleanup:
	return err;
}

THROWS err_t sharedRealloc(void **const data, const size_t count, const size_t size, allocatorFlags flags)
{
	err_t err = NO_ERRORCODE;

	QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data != NULL);
	QUITE_CHECK(size > 0);

	QUITE_RETHROW(buddyFree(g_buddy, data));
	QUITE_RETHROW(buddyAlloc(g_buddy, data, size * count));

	if (flags || ALLOCATOR_CLEAR_MEMORY == 1)
	{
		memset(*data, 0, count * size);
	}

cleanup:
	return err;
}

THROWS err_t sharedDealloc(void **const data)
{
	err_t err = NO_ERRORCODE;

	QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data != NULL);

	QUITE_RETHROW(buddyFree(g_buddy, data));

cleanup:
	if (data != NULL)
	{
		*data = NULL;
	}

	return err;
}
