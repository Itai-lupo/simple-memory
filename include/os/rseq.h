
#pragma once
#include "types/err_t.h"

#include <linux/rseq.h>

#define CREATE_RSEQ_CRITICAL_SECTION(startSectionName, endTag, abortSectionName) \
  {0, 0, (uint64_t)&&startSectionName, (uint64_t)&&endTag -  (uint64_t)&&startSectionName, (uint64_t)&&abortSectionName};


#define RSEQ_ABORT_SECTION_START(abortSectionName, startSectionName) \
  { \
    volatile bool static alwaysTrue = true; \
    if(alwaysTrue) \
    { \
      goto startSectionName; \
    } \
    asm __volatile__(".int %P0\n\t" :: "i" (RSEQ_SIG)); \
  } \
abortSectionName:

#define RSEQ_SECTION_START(startSectionName, cs) \
startSectionName: \
  setRseqCs(&cs);

#define RSEQ_SECTION_END() setRseqCs(NULL)


#ifdef __cplusplus
extern "C"
{
#endif

  THROWS err_t rseqMain(); 

  THROWS err_t rseqInit();
  THROWS err_t validateRseqCs(struct rseq_cs *cs, void *endIP);

  void setRseqCs(struct rseq_cs *cs);

  THROWS err_t getCpuId(uint32_t *cpuId);

#ifdef __cplusplus
}
#endif
