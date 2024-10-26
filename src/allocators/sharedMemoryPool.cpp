#include "allocators/sharedMemoryPool.h"

#include "defines/logMacros.h"
#include "log.h"

#include "err.h"

#include "types/buddyAllocator.h"

#include "os/sharedMemoryFile.h"
#include "types/memoryAllocator.h"
#include "types/memoryMapInfo.h"

#include "allocators/unsafeAllocator.h"

#include "memoryUtils/allocatorsConsts.h"
#include "memoryUtils/allocatorsUtilFunctions.h"

#include "os/rseq.h"

#include <alloca.h>
#include <cstddef>
#include <cstdint>
#include <math.h>
#include <sched.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>


#include <sys/ipc.h>
#include <sys/sem.h>

union semun {
    int val;
    struct semid_ds *buf;
    ushort *array;
};

typedef struct 
{
  void **const data;
  uint32_t sizeClass;
  allocatorFlags flags;
  uint32_t coreId;
} rseqAllocCall;

static const size_t freeListSize = GET_BUDDY_MAX_ELEMENT_COUNT(MAX_RANGE_EXPONENT, MIN_BUDDY_BLOCK_SIZE_EXPONENT);

//static const memoryAllocator allocator = {&sharedAlloc, &sharedRealloc, &sharedDealloc, NULL};
static memoryAllocator **coreCaches = NULL;

int semid = 0;

/**
 * @brief in order to use the buddy allocator, we need a buddy allocator, so first we put it on the stack and then we
 * can copy it to somewhere else.
 *
 * @note we can't put this on that function stack as it will be freed at the end of the function so we get it from the
 * caller.
 */
THROWS static err_t initBuddyAllocatorOnStack(buddyAllocator *resBuddyAllocator)
{
	err_t err = NO_ERRORCODE;
  union semun arg;
  struct sembuf sb;

	resBuddyAllocator->memorySource = {nullptr, getSharedMemoryFileSize, setSharedMemoryFileSize};
	resBuddyAllocator->poolSizeExponent = MAX_RANGE_EXPONENT;
	resBuddyAllocator->smallestAllocationSizeExponent = MIN_BUDDY_BLOCK_SIZE_EXPONENT;
	resBuddyAllocator->freeListSize =   (pow(2, MAX_RANGE_EXPONENT - MIN_BUDDY_BLOCK_SIZE_EXPONENT));

	QUITE_RETHROW(getSharedMemoryFileStartAddr(&resBuddyAllocator->memorySource.startAddr));
	QUITE_RETHROW(initBuddyAllocator(resBuddyAllocator));

  semid = semget(0, 1, IPC_CREAT | IPC_EXCL | 0666);

  sb.sem_op = 1;
  sb.sem_flg = 0;
  arg.val = 1;

  for(sb.sem_num = 0; sb.sem_num < 1; sb.sem_num++) { 
    /* do a semop() to "free" the semaphores. */
    /* this sets the sem_otime field, as needed below. */
    if (semop(semid, &sb, 1) == -1) {
      int e = errno;
      semctl(semid, 0, IPC_RMID); /* clean up */
      QUITE_CHECK(e == 0);
    }
  }

cleanup:
	return err;
}


/**
 * @brief we want each core to alloc from a memory that is garnted to be thread safe
 * so each cpu core can only allocate from it own buffer and there is a process that fill them up
 * @note thank you to tcmalloc for the idea.
 */
THROWS static err_t initCoreCaches(buddyAllocator *buddyOnStack)
{
	err_t err = NO_ERRORCODE;
	long coreCount = sysconf(_SC_NPROCESSORS_ONLN);
	uint32_t coreId = 0;
	slab *tempSlab = NULL;
  memoryAllocator tempCaches[256][sizeof(allocationCachesSizes) / sizeof(size_t)];
  size_t tempSizeClass = 0;

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

  // we want the caches to be saved on the shared memory in an efficent why
  tempSizeClass = getSizeClass(sizeof(memoryAllocator*) * coreCount);
  CHECK_TRACE(tempSizeClass != SIZE_MAX, "there is no allocator sizecall  big enght to allocate {}", sizeof(memoryAllocator*) * coreCount);
  
  QUITE_RETHROW(tempCaches[0][tempSizeClass].alloc((void**)&coreCaches, coreCount, sizeof(memoryAllocator*), 0, tempCaches[0][tempSizeClass].data));
	
  tempSizeClass = getSizeClass(sizeof(memoryAllocator) * (sizeof(allocationCachesSizes) / sizeof(size_t)));
  CHECK_TRACE(tempSizeClass != SIZE_MAX, "there is no allocator sizecall  big enght to allocate {}", sizeof(memoryAllocator*) * coreCount);
  
  for (int i = 0; i < coreCount; i++)
  {
    coreCaches[i] = NULL;
    RETHROW(tempCaches[i][tempSizeClass].alloc((void**)&coreCaches[i], sizeof(allocationCachesSizes) / sizeof(size_t), sizeof(memoryAllocator), 0, tempCaches[i][tempSizeClass].data));
    memcpy(coreCaches[i], &tempCaches[i], (sizeof(allocationCachesSizes) / sizeof(size_t)) * sizeof(memoryAllocator));
  }

cleanup:
	return err;
}

THROWS err_t initSharedMemory()
{
	err_t err = NO_ERRORCODE;

	buddyAllocator *buddy = (buddyAllocator *)alloca(sizeof(buddyAllocator) + freeListSize/8);

	QUITE_CHECK(g_buddy == nullptr);

	QUITE_RETHROW(initSharedMemoryFile(pow(2, MAX_RANGE_EXPONENT)));

	QUITE_RETHROW(initBuddyAllocatorOnStack(buddy));

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


THROWS static err_t handleSlabAllocError(memoryAllocator *slabAllocator, [[maybe_unused]] void **const data, [[maybe_unused]]  size_t size, [[maybe_unused]] allocatorFlags flags)
{
  err_t err = NO_ERRORCODE;
	slab *tempSlab;
    
  struct sembuf sb; 
  sb.sem_num = 0;
  sb.sem_op = -1;  /* set to allocate resource */
  sb.sem_flg = SEM_UNDO;
  

  QUITE_CHECK(semop(semid, &sb, 1) == 0);

  QUITE_RETHROW(buddyAlloc(g_buddy, (void**)&tempSlab, SLAB_SIZE));

  QUITE_RETHROW(appendSlab(slabAllocator, tempSlab));
	QUITE_RETHROW(slabAllocator->alloc(data, 1,  size , flags, (void*)tempSlab));

cleanup:
  sb.sem_op = 1;
  WARN(semop(semid, &sb, 1) == 0);

  return err;
}


USED_IN_RSEQ err_t allocRseq(void *rseqAllocData)
{
  err_t err = NO_ERRORCODE;
  rseqAllocCall *rseqCall = (rseqAllocCall*)rseqAllocData;

  size_t size = 0;
  memoryAllocator *slabAllocator = NULL;

  QUITE_RETHROW(getCpuId(&rseqCall->coreId));


  slabAllocator = &coreCaches[rseqCall->coreId][rseqCall->sizeClass];
  size =  allocationCachesSizes[rseqCall->sizeClass];
  QUITE_RETHROW(slabAllocator->alloc(rseqCall->data, 1, size, rseqCall->flags, slabAllocator->data));

cleanup:
  return err;
}

err_t abortRseqAlloc( [[maybe_unused]] bool *shouldRetry, void *rseqAllocData)
{

  err_t err = NO_ERRORCODE;
  rseqAllocCall *rseqCall = (rseqAllocCall*)rseqAllocData;
  
  if(*rseqCall->data != NULL)
  {
    QUITE_RETHROW(sharedDealloc(rseqCall->data, NULL));
  }

cleanup:
  return err;
}

THROWS static err_t handleSlabAlloc(void **const data, uint32_t sizeClass, [[maybe_unused]] allocatorFlags flags)
{
	err_t err = NO_ERRORCODE;
  rseqAllocCall rseqCall = {data, sizeClass, flags, UINT32_MAX};

 QUITE_RETHROW(doRseq(10000, &allocRseq, &abortRseqAlloc, (void*)&rseqCall));

cleanup:
  if(err.errorCode == ENOMEM)
  {
    err = NO_ERRORCODE;
    err = handleSlabAllocError(&coreCaches[rseqCall.coreId][sizeClass], data, allocationCachesSizes[rseqCall.sizeClass], flags);
  }

  return err;
}

THROWS err_t sharedAlloc(void **const data, const size_t count, const size_t size, allocatorFlags flags, [[maybe_unused]] void *sharedAllocatorData)
{
	err_t err = NO_ERRORCODE;
	uint32_t sizeClass = UINT32_MAX;
  struct sembuf sb; 
  

  QUITE_CHECK(data != NULL);
	QUITE_CHECK(*data == NULL);
	QUITE_CHECK(size > 0);
  
  sizeClass = getSizeClass(size * count);
  
  if(sizeClass == UINT32_MAX)
  {
    sb.sem_num = 0;
    sb.sem_op = -1;  /* set to allocate resource */
    sb.sem_flg = SEM_UNDO;
    QUITE_CHECK(semop(semid, &sb, 1) == 0);
    
    err = buddyAlloc(g_buddy, data, count * size);

    sb.sem_op = 1;
    QUITE_CHECK(semop(semid, &sb, 1) == 0);

    QUITE_RETHROW(err);
  } 
  else
  {
    QUITE_RETHROW(handleSlabAlloc(data, sizeClass, flags));
  }

  QUITE_CHECK(*data != NULL);
	if ((flags | ALLOCATOR_CLEAR_MEMORY) == 1)
	{
		bzero(*data, count * size);
	}

cleanup:  
  sb.sem_op = 1;

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
    err = coreCaches[0][0].realloc(data, count, size, flags, s);
    if(err.errorCode == ENOMEM)
    {
      err = NO_ERRORCODE;
      QUITE_RETHROW(sharedAlloc(data, count, size, flags, sharedAllocatorData));
      memcpy(*data, temp, MIN(size * count, s->header.cellSize));
      QUITE_RETHROW(coreCaches[0][0].free(&temp, s));
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

  QUITE_RETHROW(buddyGetCellStartAddrFromAddrInCell(g_buddy, *data, (void**)&s));

  if(s->header.slabMagic == SLAB_MAGIC)
  {
    QUITE_RETHROW(coreCaches[0][0].free(data, s));
  }
  else
  {
    QUITE_CHECK( s == *data);
    QUITE_RETHROW(buddyFree(g_buddy, data));
  }
  
cleanup:
	if (data != NULL)
	{
		*data = NULL;
	}

	return err;
}
