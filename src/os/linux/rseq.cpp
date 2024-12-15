#include "os/rseq.h"

#include "defaultTrace.h"

#include "defines/checkApi.h"
#include "err.h"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <dlfcn.h>
#include <linux/rseq.h>
#include <syscall.h>
#include <threads.h>

#include <ucontext.h>
#include <unistd.h>
#include <setjmp.h>
#define RSEQ_SIG 0

/* Allocate a large area for the TLS. */
#define RSEQ_THREAD_AREA_ALLOC_SIZE	1024

typedef struct
{
	rseqCallback rseq;

	bool shouldRetry;
	size_t maxRetrys;
	void *data;
	err_t err;
} rseqCall;

thread_local jmp_buf rseqJumpBuffer;

volatile thread_local struct rseq r  = {.cpu_id_start = 0,
																			 .cpu_id = (__u32)RSEQ_CPU_ID_UNINITIALIZED,
																			 .rseq_cs = 0,
																			 .flags = 0,
																			 .node_id = 0,
																			 .mm_cid = 0

};

extern void *__start_rseq;
extern void *__stop_rseq;

THROWS static err_t sysRseq(volatile struct rseq *rseq_abi, uint32_t rseq_len, int flags, uint32_t sig)
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

	libc_rseq_offset_p = (long *)dlsym(RTLD_NEXT, "__rseq_offset");
	libc_rseq_size_p = (uint32_t *)dlsym(RTLD_NEXT, "__rseq_size");
	libc_rseq_flags_p = (uint32_t *)dlsym(RTLD_NEXT, "__rseq_flags");
	if (libc_rseq_size_p && libc_rseq_offset_p && libc_rseq_flags_p && *libc_rseq_size_p != 0)
	{
	}

	RETHROW(sysRseq(&r, sizeof(struct rseq), 0, RSEQ_SIG));

cleanup:
	return err;
}


// we use optnone becose the compiler might reorder some stuff the can break stuff 
USED_IN_RSEQ
__attribute__((optnone))
void handleRseq(const rseq_cs *cs, rseqCall *rseqData)
{

	err_t err = NO_ERRORCODE;

	// volatile uint32_t cpuId = r.cpu_id;

	CHECK_NOTRACE_ERRORCODE(cs != NULL, EINVAL);
	CHECK_NOTRACE_ERRORCODE(rseqData != NULL, EINVAL);

	r.rseq_cs = (uint64_t)cs;
	QUITE_CHECK(r.cpu_id_start == r.cpu_id);

	// cpuId = r.cpu_id_start;

	QUITE_RETHROW(rseqData->rseq(rseqData->data));

	// CHECK_NOTRACE_ERRORCODE(r.rseq_cs != 0, 0);
	// CHECK_NOTRACE_ERRORCODE(r.cpu_id_start == r.cpu_id, 0);
	// CHECK_NOTRACE_ERRORCODE(r.cpu_id == cpuId, 0);

cleanup:
	r.rseq_cs = 0;
	rseqData->shouldRetry = false;
	rseqData->err = err;
}


THROWS err_t doRseq(size_t maxRetrys, rseqCallback rseqFunc, rseqAbortHandlerCallback abortHandler, void *data)
{
	err_t err = NO_ERRORCODE;

	volatile bool static alwaystrue = true;
	rseqCall rseqData = {rseqFunc, true, maxRetrys, data, NO_ERRORCODE};

	const rseq_cs cs = {0, 0, (uint64_t)&__start_rseq, (uint64_t)&__stop_rseq - (uint64_t)&__start_rseq,
							   (uint64_t)&&restart};

	unlikelyIf(r.cpu_id == (__u32)RSEQ_CPU_ID_UNINITIALIZED)
	{
		QUITE_RETHROW(rseqInit());
	}

	QUITE_CHECK(maxRetrys > 0);
	QUITE_CHECK(rseqFunc != NULL);

	QUITE_CHECK((uint64_t)rseqFunc >= (uint64_t)&__start_rseq && (uint64_t)rseqFunc <= (uint64_t)&__stop_rseq);
	QUITE_CHECK(
		!((uint64_t)abortHandler >= (uint64_t)&__start_rseq && (uint64_t)abortHandler <= (uint64_t)&__stop_rseq));

	if (alwaystrue)  [[likely]]
	{
		goto start;
	}
	asm __volatile__(".int 0\n\t");

restart:

	// the abort handler will be called with invalid stack
	// we want to go back to the main context instad of trying to work with invalid stack.
	// remainder to get here the rseq context do "long jump" from within a function to outside of a function
	// this mean we can't call return from here.
	longjmp(rseqJumpBuffer, 0);

	// if we get here we can't get back to the main context and we can't return so rip.
	exit(errno);

start:

	// we want all the rseq function to be on there own section
	// that allow as to call other functions that are in that section
	// but that means the rseq abort handler need to be on other function
	// that will mean we can't return from the abort handler(the return addr will be wrong)
	// so we can use setjmp to save a valid stack outside of the rseq and go back to it.
	while(!setjmp(rseqJumpBuffer) && rseqData.shouldRetry && rseqData.maxRetrys > 0)
	{
		rseqData.shouldRetry = true;
		handleRseq(&cs, &rseqData);
		QUITE_RETHROW(rseqData.err);
		if (rseqData.shouldRetry)
		{
			rseqData.maxRetrys--;
			if (abortHandler != NULL)
			{
				QUITE_RETHROW(abortHandler(&rseqData.shouldRetry, rseqData.data));
			}
		}
	}


	QUITE_CHECK(rseqData.maxRetrys != 0);
cleanup:

	return err;
}

__attribute__((section("rseq"))) THROWS err_t getCpuId(uint32_t *cpuId)
{
	err_t err = NO_ERRORCODE;
	CHECK_NOTRACE_ERRORCODE(cpuId != NULL, EINVAL);
	CHECK_NOTRACE_ERRORCODE(r.cpu_id != (uint32_t)RSEQ_CPU_ID_UNINITIALIZED, EINVAL);
	CHECK_NOTRACE_ERRORCODE(r.cpu_id != (uint32_t)RSEQ_CPU_ID_REGISTRATION_FAILED, EINVAL);

	*cpuId = r.cpu_id_start;

cleanup:
	return err;
}
