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


THROWS err_t sys_rseq(volatile struct rseq *rseq_abi, uint32_t rseq_len,
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

THROWS err_t rseq_init()
{
  err_t err = NO_ERRORCODE;
  
  libc_rseq_offset_p = (long*)dlsym(RTLD_NEXT, "__rseq_offset");
	libc_rseq_size_p = (uint32_t *)dlsym(RTLD_NEXT, "__rseq_size");
	libc_rseq_flags_p = (uint32_t*)dlsym(RTLD_NEXT, "__rseq_flags");
	if (libc_rseq_size_p && libc_rseq_offset_p && libc_rseq_flags_p &&
			*libc_rseq_size_p != 0) {
      LOG_INFO("glibc has the rseq");
		}

  RETHROW(sys_rseq(&r, sizeof(struct rseq), 0, RSEQ_SIG));

cleanup:
  return err;
}

#include <sys/mman.h>

err_t seq()
{
  err_t err = NO_ERRORCODE;

  volatile int i = 0;
  struct rseq_cs cs = {0, 0, 0, 0, 0};
  volatile int restarts = 0;


  cs.start_ip = (__u64)&&start;
  cs.post_commit_offset = (__u64)&&cleanup -  (__u64)&&start ;
  cs.abort_ip = (__u64)&&restart;

  r.rseq_cs = (__u64)&cs;


  LOG_INFO("{:X}", *((__u64*)cs.abort_ip - 1));

  LOG_INFO("{:X} {} {:X} {} ", cs.start_ip, cs.post_commit_offset, cs.abort_ip, (void*)r.rseq_cs);


  // this put RSEQ_SIG at the end of the asm instraction on most archs, and will be forcd to be before the ABORT_HANDLER
  i = RSEQ_SIG;

restart:
  restarts += 1;
  r.rseq_cs = (__u64)&cs;

start:
  for(volatile size_t j = 0; restarts < 10; j += 1)
  {
    i += 1;
  }

cleanup:

  LOG_INFO("{} {}", (int)i, (int)restarts);
  r.rseq_cs = 0;

  return err;
}

THROWS err_t rseqMain()
{
  err_t err = NO_ERRORCODE;

  QUITE_RETHROW(rseq_init());
  
  RETHROW(seq());

cleanup:
  return err;
}

