
#pragma once
#include "types/err_t.h"

#include <linux/rseq.h>



#define USED_IN_RSEQ __attribute__((section("rseq")))


typedef err_t (*rseqCallback)(void *data);
typedef err_t (*rseqAbortHandlerCallback)(bool *shouldRetry, void *data);

#ifdef __cplusplus
extern "C"
{
#endif

  THROWS err_t rseqInit();
  THROWS err_t doRseq(size_t maxRetrys, rseqCallback rseqFunc, rseqAbortHandlerCallback abortHandler, void *data);

  THROWS err_t rseqMain(); 

  THROWS err_t getCpuId(uint32_t *cpuId);

#ifdef __cplusplus
}
#endif
