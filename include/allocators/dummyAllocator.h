#pragma once

#include "types/memoryAllocator.h"

#ifdef __cplusplus
extern "C"
{
#endif

	/**
	 * @brief Get the Dummy Allocator
	 * 			dose nothing but check that the data is valid, the real data should be allocated somewhere else
	 * 			relloc will allways fail, as this dosn't really mange the memory.
	 *
	 * @return memoryAllocator*
	 */
	memoryAllocator *getDummyAllocator();

#ifdef __cplusplus
}
#endif
