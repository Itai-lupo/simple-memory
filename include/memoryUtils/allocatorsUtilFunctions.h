#pragma once

#include "log.h"

#include "err.h"

#include "allocatorsConsts.h"

// all the buddy allocators are alligned so all the pointers on a certien slab can give the slab when you mask them out
// and you add 1 as this is the buddy mangement data
#define GET_SLAB_START(data) (slab*)(((size_t)data & (~0lu << 13)) + 1)

static constexpr uint32_t getSizeClass(const size_t size)
{
   for(size_t i = 0; i < sizeof(allocationCachesSizes) / sizeof(size_t); i++)
  {
    if(size <= allocationCachesSizes[i])
    {
      return i;
    }
  }

  return UINT32_MAX;
}

/**
 * @brief after we have a real shared allocator we want to move all of the buddy allocator data to it from the stack
 */
THROWS static err_t moveBuddyFromStackToFinalAllocator(buddyAllocator **resBuddyAllocator, buddyAllocator *buddyOnStack)
{
	err_t err = NO_ERRORCODE;
  QUITE_RETHROW(buddyAlloc(buddyOnStack,(void**)resBuddyAllocator,
                sizeof(buddyAllocator) + GET_BUDDY_MAX_ELEMENT_COUNT(MAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT) / 8));

	memcpy(*resBuddyAllocator, buddyOnStack,
		   sizeof(buddyAllocator) + GET_BUDDY_MAX_ELEMENT_COUNT(MAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT) / 8);

cleanup:
	return err;
}
