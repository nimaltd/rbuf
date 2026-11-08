/**
 * @file        main.h
 * @brief       Host stub standing in for the CubeMX generated main.h, for the threaded tests.
 * @version     3.0.0
 *
 * @author      Nima Askari (NimaLTD)
 * @email       nima.askari@gmail.com
 * @github      https://www.github.com/nimaltd
 *
 * @copyright   (c) 2026 Nima Askari (NimaLTD)
 *              SPDX-License-Identifier: Apache-2.0
 *              See LICENSE.md in the project root for the full license text.
 *
 * @note        This file exists only so the library can be run by real threads
 *              on a PC. It is not part of the shipped library, and it is never
 *              on the include path of a real STM32 build.
 */

#ifndef MAIN_H
#define MAIN_H

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include <stdint.h>
#include <stdlib.h>

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* CMSIS spells volatile this way. */
#define __IO                volatile

/* A NULL here is a bug in the test itself. */
#define assert_param(expr)  ((expr) ? (void)0U : abort())

/* The threads run on two cores at once, so the barrier has to be a real one:
   the compiler and the CPU both keep to it. */
#define __DMB()             __atomic_thread_fence(__ATOMIC_SEQ_CST)

/* PRIMASK as one lock all the threads share. A single core with its
   interrupts off runs nothing else until they are on again, and holding the
   one lock gives the same. */
extern uint32_t rbuf_thread_get_primask(void);
extern void     rbuf_thread_disable_irq(void);
extern void     rbuf_thread_set_primask(uint32_t value);

#define __get_PRIMASK()     rbuf_thread_get_primask()
#define __disable_irq()     rbuf_thread_disable_irq()
#define __set_PRIMASK(x)    rbuf_thread_set_primask((uint32_t)(x))

#endif /* MAIN_H */
