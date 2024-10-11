#include "os/rseq.h"

#include "log.h"
#include "err.h"

#include <linux/rseq.h>
#include <syscall.h>
#include <dlfcn.h>

#define RSEQ_SIG 0	

static  volatile struct rseq r __attribute__(( aligned(1024))) = {
  .cpu_id_start = 0,
	.cpu_id = (__u32)RSEQ_CPU_ID_UNINITIALIZED,
};


void setRseqCs(struct rseq_cs *cs)
{
  r.rseq_cs = (uint64_t)cs;
}

THROWS err_t getCpuId(uint32_t *cpuId)
{
  err_t err = NO_ERRORCODE;
  QUITE_CHECK(cpuId != NULL);
  QUITE_CHECK(r.cpu_id != (uint32_t)RSEQ_CPU_ID_UNINITIALIZED);
  QUITE_CHECK(r.cpu_id != (uint32_t)RSEQ_CPU_ID_REGISTRATION_FAILED);

  *cpuId = r.cpu_id;

cleanup:
  return err;
}
THROWS static err_t sysRseq(volatile struct rseq *rseq_abi, uint32_t rseq_len,
		    int flags, uint32_t sig)
{
  err_t err = NO_ERRORCODE;
  
  CHECK(syscall(__NR_rseq, rseq_abi, rseq_len, flags, sig) == 0);

cleanup:
  return err;
}

static const ptrdiff_t *libc_rseq_offset_p;
static const unsigned int *libc_rseq_size_p;
static const unsigned int *libc_rseq_flags_p;

THROWS err_t rseqInit()
{
  err_t err = NO_ERRORCODE;
  
  libc_rseq_offset_p = (long*)dlsym(RTLD_NEXT, "__rseq_offset");
	libc_rseq_size_p = (uint32_t *)dlsym(RTLD_NEXT, "__rseq_size");
	libc_rseq_flags_p = (uint32_t*)dlsym(RTLD_NEXT, "__rseq_flags");
	if (libc_rseq_size_p && libc_rseq_offset_p && libc_rseq_flags_p &&
			*libc_rseq_size_p != 0) {
      LOG_INFO("glibc has the rseq");
		}

  RETHROW(sysRseq(&r, sizeof(struct rseq), 0, RSEQ_SIG));

cleanup:
  return err;
}


THROWS err_t validateRseqCs(struct rseq_cs *cs, void *endIP)
{
  err_t err = NO_ERRORCODE;

  CHECK_TRACE((size_t)endIP > cs->start_ip, "the start of the rseq is after the end of it");
  CHECK_TRACE(cs->start_ip > cs->abort_ip, "the abort code should be before the start of the rseq");
  CHECK_TRACE(*((uint32_t*)cs->abort_ip - 1) == RSEQ_SIG, "the int before is 0x{:X}", *((uint32_t*)cs->abort_ip - 1));

cleanup:
  return err;
}

err_t seq()
{
  err_t err = NO_ERRORCODE;
  static struct rseq_cs cs = CREATE_RSEQ_CRITICAL_SECTION(start, cleanup, restart);
  volatile size_t static i = 0;
  volatile int static restarts = 0;

  RETHROW(validateRseqCs(&cs, &&cleanup));
  
  RSEQ_ABORT_SECTION_START(restart, start);
  restarts += 1;

  RSEQ_SECTION_START(start, cs);
  for(size_t j = 0; j < 500000000; j += 1)
  {
    i += 1;
  }

cleanup:
  r.rseq_cs = 0;
  
  LOG_INFO("{} {}", (int)i, (int)restarts);
  return NO_ERRORCODE;
}


THROWS err_t rseqMain()
{
  err_t err = NO_ERRORCODE;

  QUITE_RETHROW(rseqInit());
  RETHROW(seq());
cleanup:
  return err;
}

