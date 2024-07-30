#pragma once
#include "types/err_t.h"

typedef err_t (*getMemorySizeCallback)(size_t *size);
typedef err_t (*setMemorySizeCallback)(size_t size);

typedef struct
{
	void *startAddr;
	getMemorySizeCallback getSize;
	setMemorySizeCallback setSize;
} memoryMapInfo;
