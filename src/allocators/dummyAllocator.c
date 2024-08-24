
#include "defaultTrace.h"

#include "allocators/dummyAllocator.h"
#include "err.h"

#include <stdlib.h>
#include <string.h>

err_t dummyAlloc(void **const ptr, const size_t count, const size_t size, allocatorFlags flags);

err_t dummyRealloc(void **const ptr, const size_t count, const size_t size, allocatorFlags flags);

err_t dummyDealloc(void **const ptr);

memoryAllocator dummy = {dummyAlloc, dummyRealloc, dummyDealloc};

err_t dummyAlloc(void **const ptr, const size_t count, const size_t size, [[maybe_unused]] allocatorFlags flags)
{
	err_t err = NO_ERRORCODE;
	QUITE_CHECK(ptr != NULL)
	QUITE_CHECK(count > 0 && size > 0);
	QUITE_CHECK(*ptr != NULL);

cleanup:
	return err;
}

err_t dummyRealloc(void **const ptr, const size_t count, const size_t size, [[maybe_unused]] allocatorFlags flags)
{
	err_t err = NO_ERRORCODE;

	QUITE_CHECK(ptr != NULL);
	QUITE_CHECK(*ptr != NULL);
	QUITE_CHECK(count > 0 && size > 0);

	// "the dummy allocator don't support realloc"
	QUITE_CHECK(false);

cleanup:
	return err;
}

err_t dummyDealloc(void **const ptr)
{
	err_t err = NO_ERRORCODE;
	QUITE_CHECK(ptr != NULL);
	QUITE_CHECK(*ptr != NULL);

	*ptr = NULL;

cleanup:
	return err;
}

memoryAllocator *getDummyAllocator()
{
	return &dummy;
}
