#pragma once
#include "types/err_t.h"
#include "types/fd_t.h"
// #include <cstddef>
#include <unistd.h>

#ifdef __cplusplus
extern "C"
{
#endif

	THROWS err_t initSharedMemoryFile(size_t maxSize);
	err_t closeSharedMemoryFile();

	THROWS err_t setSharedMemoryFileSize(size_t size);
	THROWS err_t getSharedMemoryFileSize(size_t *size);

	THROWS err_t getSharedMemoryFileFd(fd_t &fd);
	THROWS err_t getSharedMemoryFileStartAddr(void **ptr);

#ifdef __cplusplus
}
#endif
