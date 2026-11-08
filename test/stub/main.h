/**
 * @file        main.h
 * @brief       Host stub standing in for the CubeMX generated main.h.
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
 * @note        This file exists only so the library can be compiled and tested
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

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* CMSIS spells volatile this way. The same token rbuf.h falls back to, so
   whichever comes first, the other is no redefinition. */
#define __IO                volatile

/* As the HAL defines it with USE_FULL_ASSERT set, so the tests see a NULL
   argument stopped. The tests define assert_failed(). */
#define assert_param(expr)  ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))

/* The tests are single threaded, so a barrier orders nothing here. It goes
   through a function instead, which lets a test look at what was already in
   memory, and what was not yet, at the moment the library asked for one. */
extern void rbuf_test_barrier(void);
#define __DMB()             rbuf_test_barrier()

/* PRIMASK, modelled closely enough that a test can tell whether the library
   really closed the window an interrupt could arrive through. Turning the
   interrupts back on goes through a function, because that is the moment a
   pending interrupt fires, and the tests have to model it. */
extern uint32_t rbuf_test_primask;
extern uint32_t rbuf_test_disables;
extern void     rbuf_test_set_primask(uint32_t value);

#define __get_PRIMASK()     (rbuf_test_primask)
#define __set_PRIMASK(x)    rbuf_test_set_primask((uint32_t)(x))
#define __disable_irq()     (rbuf_test_primask = 1U, rbuf_test_disables++)

/* Fires while a push or a pop copies, after it took its slots and before it
   says it is done, where an interrupt landing would hurt most. Compiled out of
   a real build, so it costs nothing on the target. */
extern void rbuf_test_hook(void);
#define RBUF_TEST_HOOK()    rbuf_test_hook()

/*
 * ****************************************************************************************************
 * Public function prototypes
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Where a failed assert_param lands. The tests define it.
 */
void assert_failed(uint8_t *file, uint32_t line);

#endif /* MAIN_H */
