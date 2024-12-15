/**
 * @file rseq.h
 * @author itai lupo
 * @brief  my impletion of a wrapper to calling a restartable sequence on linux
 * @version 1.0
 * @date 2024-10-29
 * 
 * @copyright Copyright (c) 2024
 * 
 */

#pragma once
#include "types/err_t.h"

#include <linux/rseq.h>



#define USED_IN_RSEQ __attribute__((section("rseq")))

extern volatile thread_local struct rseq r ;

typedef err_t (*rseqCallback)(void *data);
typedef err_t (*rseqAbortHandlerCallback)(bool *shouldRetry, void *data);

#ifdef __cplusplus
extern "C"
{
#endif
  /**
   * @brief register in the linux rseq
   * 
   * @return if the rseq failed to register 
   */
  
  THROWS err_t rseqInit();

  /**
   * @brief a wrapper to linux rseq, 
   * this promise that rseqFunc will start and end on the same cpu core without any interrupts.
   * @note 
   * if there is any interrupts then the abort handler will be called and then it eill retry to call the rseqFunc
   *
   * @note
   * the rseq func and the function that it calls has to be marked with USED_IN_RSEQ, so they will in the rseq section on the final elf.
   * 
   * @param maxRetires how many time should it try to call rseqFunc
   * @param rseqFunc the function to call as rseq
   * @param abortHandler the function to call if the rseq was interrupted.
   * @param data a pointer that will be passed to both functions.
   * @return THROWS if there was an error in one of the function, or while setting up the rseq, or it the where to many retires.
   */
  THROWS err_t doRseq(size_t maxRetires, rseqCallback rseqFunc, rseqAbortHandlerCallback abortHandler, void *data);

  /**
   * @brief Get the current core id
   * 
   * 
   * @param cpuId 
   * @return THROWS if rseq is not registered
   */
  THROWS err_t getCpuId(uint32_t *cpuId);

#ifdef __cplusplus
}
#endif
