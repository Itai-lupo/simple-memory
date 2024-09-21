#include "allocators/unsafeAllocator.h"

//#include "defaultTrace.h"

#include "log.h"

#include "err.h"

#include <math.h>

#define GET_SLAB_FREE_LIST_SIZE(cellSize)  ceil((float)(SLAB_SIZE - sizeof(slabHead)) / (float)cellSize / 8.0f)

THROWS static err_t unsafeAlloc(void **const ptr, const size_t count, const size_t size,
							   [[maybe_unused]] allocatorFlags flags, void *firstSlab)
{
	err_t err = NO_ERRORCODE;
  slab *slabContent = (slab*)firstSlab;
  size_t freeListSize = 0;

	QUITE_CHECK(ptr != NULL)
	QUITE_CHECK(count > 0 && size > 0);
	QUITE_CHECK(*ptr == NULL);
	QUITE_CHECK(firstSlab != NULL);
  QUITE_CHECK(slabContent->header.slabMagic == SLAB_MAGIC);
  
  QUITE_CHECK(slabContent->header.cellSize > size * count);

  freeListSize = GET_SLAB_FREE_LIST_SIZE(slabContent->header.cellSize);


  for(size_t i = 0; i < freeListSize; i++)
  {
    if(slabContent->cache[i] != 255){
      for(size_t j = 0; j < 8; j++)
      {
        if((slabContent->cache[i] & (1 << j)) == 0)
        {
          *ptr = (void*)&(slabContent->cache[(i * 8 + j) * slabContent->header.cellSize + freeListSize]);
          slabContent->cache[i] |=  (1 << j);
          goto cleanup;
        } 
      }
    }
  }

  CHECK_ERRORCODE(false, ENOMEM);

cleanup:
	return err;
}

THROWS err_t unsafeRealloc(void **const ptr, const size_t count, const size_t size,
						  [[maybe_unused]] allocatorFlags flags, void *firstSlab)
{
	err_t err = NO_ERRORCODE;

	QUITE_CHECK(ptr != NULL);
	QUITE_CHECK(*ptr != NULL);
	QUITE_CHECK(count > 0 && size > 0);
	QUITE_CHECK(firstSlab != NULL);


cleanup:
	return err;
}

THROWS err_t unsafeDealloc(void **const ptr, void *firstSlab)
{
	err_t err = NO_ERRORCODE;
	QUITE_CHECK(ptr != NULL);
	QUITE_CHECK(*ptr != NULL);
	QUITE_CHECK(firstSlab != NULL);

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

//  LOG_INFO("for slab of size {} with cell size {} free list size is {}", SLAB_SIZE - sizeof(slabHead), cellSize, freeListSize);

  for(size_t i = 0; i < freeListSize; i++)
  {
    firstSlab->cache[i] = 0;
  }

cleanup:
	return err;
}

err_t appendSlab(memoryAllocator *unsafeAllocator, slab *newSlab)
{
	err_t err = NO_ERRORCODE;
	QUITE_CHECK(unsafeAllocator != NULL);
	QUITE_CHECK(newSlab != NULL);

cleanup:
	return err;
}

