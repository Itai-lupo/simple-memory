#pragma once

#include <sched.h>

#include "types/buddyAllocator.h"

// this 16GiB(2^34B) most system won't even by able to allocate that much ever so there will be errors out of memry most
// likly before we reach that
static constexpr const size_t MAX_RANGE_EXPONENT = 34;

// min block size is 16MiB to save on mangment size, this is the allocater that give blocks to the local pools there
// fore it dosn't need less then that
static constexpr const size_t MIN_BUDDY_BLOCK_SIZE_EXPONENT = 15;

static const off_t poolSize = sysconf(_SC_PAGESIZE) * 16;



#define ALLOC_BUDDY_ON_STACK(list_size) (buddyAllocator *)alloca(sizeof(buddyAllocator) + list_size)

static buddyAllocator *g_buddy = nullptr;

#ifndef SLAB_ALLOCATION_CACHES_SIZES
#define SLAB_ALLOCATION_CACHES_SIZES                                                                                   \
	{                                                                                                                  \
		32, 64, 128, 255, 510, 1020, 2040, 4080, 8180                                                                  \
	}
#endif

const constexpr inline size_t allocationCachesSizes[] = SLAB_ALLOCATION_CACHES_SIZES;
