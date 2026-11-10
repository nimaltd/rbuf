/**
 * @file        rbuf.h
 * @brief       Ring buffer for items of any type, safe between interrupts and threads, for STM32.
 * @version     3.0.0
 *
 * @author      Nima Askari (NimaLTD)
 * @email       nima.askari@gmail.com
 * @github      https://www.github.com/nimaltd
 * @linkedin    https://www.linkedin.com/in/nimaltd
 * @youtube     https://www.youtube.com/@nimaltd
 * @instagram   https://instagram.com/github.nimaltd
 *
 * @copyright   (c) 2026 Nima Askari (NimaLTD)
 *              SPDX-License-Identifier: Apache-2.0
 *              See LICENSE.md in the project root for the full license text.
 *
 * @note        Any number of interrupts and threads may push and pop one buffer at
 *              once. A call holds the interrupts off for a few instructions only,
 *              never while it copies, so the pause is the same for any item size.
 */

#ifndef RBUF_H
#define RBUF_H

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* CMSIS spells volatile as __IO, and main.h brings it in. Defined here only
   when it has not, so rbuf.h can come first or alone. It is the same single
   token CMSIS defines, so CMSIS defining it afterwards is no redefinition. */
#ifndef __IO
#define __IO                volatile
#endif

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Error values returned by the ring buffer.
 */
typedef enum
{
    RBUF_ERR_NONE    = 0, /**< Done.                                                 */
    RBUF_ERR_INVALID = 1, /**< rbuf_init() refused its arguments, or was not called. */
    RBUF_ERR_FULL    = 2, /**< Not room for all the items. Nothing was pushed.       */
    RBUF_ERR_EMPTY   = 3, /**< Not that many items in it. Nothing was popped.        */

} rbuf_err_t;

/*****************************************************************************************************/
/**
 * @brief Ring buffer handle. Declare one per buffer, and set it up with rbuf_init().
 */
typedef struct
{
    uint8_t       *buffer;        /**< The memory given to rbuf_init().                 */
    uint32_t      item_size;      /**< Bytes in one item.                               */
    uint32_t      count;          /**< Items it holds when full. 0 until init works.    */
    __IO uint32_t head;           /**< Items a pop may take end here.                   */
    __IO uint32_t head_reserved;  /**< Slots pushes have taken end here, some unfilled. */
    __IO uint32_t pushing;        /**< Pushes copying in right now.                     */
    __IO uint32_t tail;           /**< Slots a push may fill end here.                  */
    __IO uint32_t tail_reserved;  /**< Items pops have taken end here, some unread.     */
    __IO uint32_t popping;        /**< Pops copying out right now.                      */

} rbuf_t;

/*
 * ****************************************************************************************************
 * Public function prototypes
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Set up a handle on memory for count items of item_size bytes each, and empty it.
 */
rbuf_err_t rbuf_init(rbuf_t *handle, void *buffer, uint32_t item_size, uint32_t count);

/*****************************************************************************************************/
/**
 * @brief Copy one item in, at the end.
 */
rbuf_err_t rbuf_push(rbuf_t *handle, const void *item);

/*****************************************************************************************************/
/**
 * @brief Copy n items in, at the end, all of them or none.
 */
rbuf_err_t rbuf_push_n(rbuf_t *handle, const void *items, uint32_t n);

/*****************************************************************************************************/
/**
 * @brief Copy the oldest item out, and take it out of the buffer.
 */
rbuf_err_t rbuf_pop(rbuf_t *handle, void *item);

/*****************************************************************************************************/
/**
 * @brief Copy the n oldest items out, and take them out of the buffer, all of them or none.
 */
rbuf_err_t rbuf_pop_n(rbuf_t *handle, void *items, uint32_t n);

/*****************************************************************************************************/
/**
 * @brief Drop every item a pop could take now.
 */
void rbuf_flush(rbuf_t *handle);

/*****************************************************************************************************/
/**
 * @brief How many items are waiting to be popped.
 */
uint32_t rbuf_used(const rbuf_t *handle);

/*****************************************************************************************************/
/**
 * @brief How many more items there is room for.
 */
uint32_t rbuf_space(const rbuf_t *handle);

/*****************************************************************************************************/
/**
 * @brief Whether there is nothing to pop.
 */
bool rbuf_is_empty(const rbuf_t *handle);

/*****************************************************************************************************/
/**
 * @brief Whether there is no room for one more item.
 */
bool rbuf_is_full(const rbuf_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* RBUF_H */
