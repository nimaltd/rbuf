/**
 * @file        rbuf.c
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
 */

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include "rbuf.h"
#include <string.h>
#include "main.h"

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* The most items one buffer can hold. The indexes count up to 2 * count - 1,
   and that has to fit in 32 bits. */
#define RBUF_COUNT_MAX      0x7FFFFFFFU

/* A place for the tests to play an interrupt that lands while a push or a pop
   is copying: after it has taken its slots, before it says it is done.
   Nothing is generated unless the tests define it. */
#ifndef RBUF_TEST_HOOK
#define RBUF_TEST_HOOK()    do { } while (0)
#endif

/*
 * ****************************************************************************************************
 * Private function prototypes
 * ****************************************************************************************************
*/

static uint32_t rbuf_lock(void);
static void     rbuf_unlock(uint32_t primask);
static uint32_t rbuf_used_of(uint32_t count, uint32_t head, uint32_t tail);
static uint32_t rbuf_slot(uint32_t count, uint32_t index);
static uint32_t rbuf_advance(uint32_t count, uint32_t index, uint32_t n);
static void     rbuf_copy_in(rbuf_t *handle, uint32_t index, const uint8_t *src, uint32_t n);
static void     rbuf_copy_out(const rbuf_t *handle, uint32_t index, uint8_t *dst, uint32_t n);

/*
 * ****************************************************************************************************
 * Public function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Set up a handle on memory for count items of item_size bytes each, and empty it.
 *
 * @param[out] handle     Handle to set up.
 * @param[in]  buffer     At least item_size * count bytes, kept for as long as the handle is used.
 * @param[in]  item_size  Bytes in one item, sizeof() what goes in.
 * @param[in]  count      Items it holds when full, from 1 to 0x7FFFFFFF.
 * @return RBUF_ERR_NONE, or RBUF_ERR_INVALID for a size of 0 or one too big.
 */
rbuf_err_t rbuf_init(rbuf_t *handle, void *buffer, uint32_t item_size, uint32_t count)
{
    rbuf_err_t err = RBUF_ERR_INVALID;

    assert_param(handle != NULL);
    assert_param(buffer != NULL);

    /* Refused from here until the end. A handle whose init fails is refused by
       every other call, whatever it held before. */
    handle->count = 0U;

    /* Runs once. A check that fails breaks out to the one return at the end. */
    do
    {
        if ((item_size == 0U) || (count == 0U))
        {
            break;
        }

        if (count > RBUF_COUNT_MAX)
        {
            break;
        }

        /* The whole buffer, in bytes, has to fit in 32 bits too. */
        if (item_size > (UINT32_MAX / count))
        {
            break;
        }

        handle->buffer        = (uint8_t *)buffer;
        handle->item_size     = item_size;
        handle->head          = 0U;
        handle->head_reserved = 0U;
        handle->pushing       = 0U;
        handle->tail          = 0U;
        handle->tail_reserved = 0U;
        handle->popping       = 0U;

        /* Last, so the handle is never seen set up before all of it is. */
        handle->count         = count;
        err                   = RBUF_ERR_NONE;
    }
    while (false);

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Copy one item in, at the end.
 *
 * @param[in,out] handle  Handle from rbuf_init().
 * @param[in]     item    item_size bytes to copy in.
 * @return RBUF_ERR_NONE, RBUF_ERR_FULL when there is no room, or RBUF_ERR_INVALID.
 */
rbuf_err_t rbuf_push(rbuf_t *handle, const void *item)
{
    return rbuf_push_n(handle, item, 1U);
}

/*****************************************************************************************************/
/**
 * @brief Copy n items in, at the end, all of them or none.
 *
 * Any number of interrupts and threads may push at once. Each takes its slots
 * first, so a push that lands in the middle of another takes the slots after
 * it and copies in at the same time. Items pushed that way reach a pop once
 * the push they landed in is done too, in the order the slots were taken.
 *
 * @param[in,out] handle  Handle from rbuf_init().
 * @param[in]     items   n items, one after the other, n * item_size bytes in all.
 * @param[in]     n       Items to copy in. 0 does nothing and returns RBUF_ERR_NONE.
 * @return RBUF_ERR_NONE, RBUF_ERR_FULL when there is no room for all n, or RBUF_ERR_INVALID.
 */
rbuf_err_t rbuf_push_n(rbuf_t *handle, const void *items, uint32_t n)
{
    rbuf_err_t err = RBUF_ERR_INVALID;

    assert_param(handle != NULL);
    assert_param(items != NULL);

    /* Runs once. A check that fails breaks out to the one return at the end. */
    do
    {
        /* Refused by rbuf_init(), or it never ran. */
        if (handle->count == 0U)
        {
            break;
        }

        /* Take the slots, all or none. Only these few sums are done with the
           interrupts off. tail may move on as a pop finishes, but only ever
           towards more room, so the room seen here is never more than there
           really is. */
        uint32_t primask = rbuf_lock();
        uint32_t start   = handle->head_reserved;
        bool     room    = (n <= (handle->count - rbuf_used_of(handle->count, start, handle->tail)));

        if (room)
        {
            handle->head_reserved = rbuf_advance(handle->count, start, n);
            handle->pushing++;
        }

        rbuf_unlock(primask);

        if (!room)
        {
            err = RBUF_ERR_FULL;
            break;
        }

        /* With the interrupts on, however long it takes. The slots are this
           push's alone: another push takes the ones after them, and no pop
           reaches them before head has moved past. */
        rbuf_copy_in(handle, start, (const uint8_t *)items, n);

        RBUF_TEST_HOOK();

        /* Done. head cannot move past slots another push is still filling,
           so it waits for the last push to finish, and that one moves it past
           every slot taken so far, its own and the others'. */
        primask = rbuf_lock();
        handle->pushing--;

        if (handle->pushing == 0U)
        {
            /* The items have to be in memory before head says they are there,
               or a pop could copy out what was in those slots before. */
            __DMB();

            handle->head = handle->head_reserved;
        }

        rbuf_unlock(primask);

        err = RBUF_ERR_NONE;
    }
    while (false);

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Copy the oldest item out, and take it out of the buffer.
 *
 * @param[in,out] handle  Handle from rbuf_init().
 * @param[out]    item    item_size bytes to copy the item into. Left alone on an error.
 * @return RBUF_ERR_NONE, RBUF_ERR_EMPTY when there is nothing in it, or RBUF_ERR_INVALID.
 */
rbuf_err_t rbuf_pop(rbuf_t *handle, void *item)
{
    return rbuf_pop_n(handle, item, 1U);
}

/*****************************************************************************************************/
/**
 * @brief Copy the n oldest items out, and take them out of the buffer, all of them or none.
 *
 * Any number of interrupts and threads may pop at once. Each takes its items
 * first, so a pop that lands in the middle of another takes the items after
 * it. Their slots are given back to pushes once the last pop is done.
 *
 * @param[in,out] handle  Handle from rbuf_init().
 * @param[out]    items   n * item_size bytes to copy them into. Left alone on an error.
 * @param[in]     n       Items to copy out. 0 does nothing and returns RBUF_ERR_NONE.
 * @return RBUF_ERR_NONE, RBUF_ERR_EMPTY when there are fewer than n, or RBUF_ERR_INVALID.
 */
rbuf_err_t rbuf_pop_n(rbuf_t *handle, void *items, uint32_t n)
{
    rbuf_err_t err = RBUF_ERR_INVALID;

    assert_param(handle != NULL);
    assert_param(items != NULL);

    /* Runs once. A check that fails breaks out to the one return at the end. */
    do
    {
        /* Refused by rbuf_init(), or it never ran. */
        if (handle->count == 0U)
        {
            break;
        }

        /* Take the items, all or none, the same way a push takes its slots.
           head may move on as a push finishes, but only ever towards more
           items, so the count seen here is never more than there really is. */
        uint32_t primask = rbuf_lock();
        uint32_t start   = handle->tail_reserved;
        bool     enough  = (n <= rbuf_used_of(handle->count, handle->head, start));

        if (enough)
        {
            handle->tail_reserved = rbuf_advance(handle->count, start, n);
            handle->popping++;
        }

        rbuf_unlock(primask);

        if (!enough)
        {
            err = RBUF_ERR_EMPTY;
            break;
        }

        /* head was read before any item. Without the barrier a core could
           read an item first and miss what a push had just written to it. */
        __DMB();

        rbuf_copy_out(handle, start, (uint8_t *)items, n);

        RBUF_TEST_HOOK();

        /* Done. As with head, tail waits for the last pop to finish, and that
           one gives back every slot taken so far. */
        primask = rbuf_lock();
        handle->popping--;

        if (handle->popping == 0U)
        {
            /* Copied out before tail gives the slots back, or a push could
               write over an item that is still being read. */
            __DMB();

            handle->tail = handle->tail_reserved;
        }

        rbuf_unlock(primask);

        err = RBUF_ERR_NONE;
    }
    while (false);

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Drop every item a pop could take now.
 *
 * As a pop of every item there is would, and the memory is left as it is. A
 * push still copying in keeps its slots, and its items arrive once it is done.
 * So do the items of a push that landed in the middle of it, even one that has
 * returned already: they were not there to pop yet, so they are not dropped.
 * A pop still copying out keeps the items it took.
 *
 * @param[in,out] handle  Handle from rbuf_init().
 */
void rbuf_flush(rbuf_t *handle)
{
    assert_param(handle != NULL);

    uint32_t primask = rbuf_lock();

    handle->tail_reserved = handle->head;

    /* With a pop still copying, its slots are not free yet. The last pop to
       finish gives them back, up to here. */
    if (handle->popping == 0U)
    {
        handle->tail = handle->head;
    }

    rbuf_unlock(primask);
}

/*****************************************************************************************************/
/**
 * @brief How many items are waiting to be popped.
 *
 * @param[in] handle  Handle from rbuf_init().
 * @return Items a pop could take now, 0 when rbuf_init() refused it.
 */
uint32_t rbuf_used(const rbuf_t *handle)
{
    assert_param(handle != NULL);

    /* Items still being copied in or out are not counted. */
    uint32_t primask = rbuf_lock();
    uint32_t used    = rbuf_used_of(handle->count, handle->head, handle->tail_reserved);

    rbuf_unlock(primask);

    return used;
}

/*****************************************************************************************************/
/**
 * @brief How many more items there is room for.
 *
 * @param[in] handle  Handle from rbuf_init().
 * @return Items a push could put in now, 0 when rbuf_init() refused it.
 */
uint32_t rbuf_space(const rbuf_t *handle)
{
    assert_param(handle != NULL);

    /* Slots still being copied in or out are not counted. */
    uint32_t primask = rbuf_lock();
    uint32_t taken   = rbuf_used_of(handle->count, handle->head_reserved, handle->tail);
    uint32_t space   = handle->count - taken;

    rbuf_unlock(primask);

    return space;
}

/*****************************************************************************************************/
/**
 * @brief Whether there is nothing to pop.
 *
 * @param[in] handle  Handle from rbuf_init().
 * @return true when no item is in it, and for a handle rbuf_init() refused.
 */
bool rbuf_is_empty(const rbuf_t *handle)
{
    return rbuf_used(handle) == 0U;
}

/*****************************************************************************************************/
/**
 * @brief Whether there is no room for one more item.
 *
 * @param[in] handle  Handle from rbuf_init().
 * @return true when every slot holds an item, and for a handle rbuf_init() refused.
 */
bool rbuf_is_full(const rbuf_t *handle)
{
    return rbuf_space(handle) == 0U;
}

/*
 * ****************************************************************************************************
 * Private function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Turn the interrupts off, for the few sums that take or give back slots.
 *
 * @return What rbuf_unlock() puts back.
 */
static uint32_t rbuf_lock(void)
{
    /* PRIMASK, not the RTOS's own critical section. FreeRTOS's masks only the
       interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY, so one above it
       could still land in the middle, and must not call FreeRTOS at all. PRIMASK
       holds off every interrupt but the NMI and HardFault, and needs no RTOS.
       Saved and put back as found, rather than turned on again after, so a
       call from code that already has them off leaves them off. */
    uint32_t primask = __get_PRIMASK();

    __disable_irq();

    return primask;
}

/*****************************************************************************************************/
/**
 * @brief Put the interrupts back as rbuf_lock() found them.
 *
 * @param[in] primask  What rbuf_lock() returned.
 */
static void rbuf_unlock(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/*****************************************************************************************************/
/**
 * @brief How many items lie between tail and head.
 *
 * @param[in] count  Items the buffer holds when full.
 * @param[in] head   The end, one of the head indexes.
 * @param[in] tail   The start, one of the tail indexes.
 * @return Items between them, from 0 to count.
 */
static uint32_t rbuf_used_of(uint32_t count, uint32_t head, uint32_t tail)
{
    uint32_t used;

    /* Both run from 0 to 2 * count - 1, twice round the slots, and start again.
       Going round twice is what tells a full buffer, head a lap ahead, from an
       empty one, head on tail, so every slot can hold an item and none has to
       be kept free. head is below tail once it has wrapped and tail not yet. */
    if (head >= tail)
    {
        used = head - tail;
    }
    else
    {
        used = ((2U * count) - tail) + head;
    }

    /* Read under the lock they are never further apart than count. A second
       core is not held off by PRIMASK, though, and there the two can come from
       two different moments. Kept to what the buffer can hold, at least. */
    if (used > count)
    {
        used = count;
    }

    return used;
}

/*****************************************************************************************************/
/**
 * @brief The slot an index falls on.
 *
 * @param[in] count  Items the buffer holds when full.
 * @param[in] index  A head or tail index, from 0 to 2 * count - 1.
 * @return The slot, from 0 to count - 1.
 */
static uint32_t rbuf_slot(uint32_t count, uint32_t index)
{
    /* The second lap uses the same slots as the first. A compare, not a %,
       which a Cortex-M0 has no instruction for. */
    return (index < count) ? index : (index - count);
}

/*****************************************************************************************************/
/**
 * @brief Move an index on by n items.
 *
 * @param[in] count  Items the buffer holds when full.
 * @param[in] index  A head or tail index, from 0 to 2 * count - 1.
 * @param[in] n      Items to move on by, at most count.
 * @return The new index, from 0 to 2 * count - 1.
 */
static uint32_t rbuf_advance(uint32_t count, uint32_t index, uint32_t n)
{
    /* index + n, round 2 * count. Worked out from what is left before the
       wrap, since index + n itself can pass 32 bits when count is near its
       limit. */
    uint32_t left = (2U * count) - index;

    return (n < left) ? (index + n) : (n - left);
}

/*****************************************************************************************************/
/**
 * @brief Copy n items into the slots from index on.
 *
 * @param[in,out] handle  Handle to copy into.
 * @param[in]     index   Where the first item goes, the start of the slots a push took.
 * @param[in]     src     The items.
 * @param[in]     n       Items to copy, at most the room there is.
 */
static void rbuf_copy_in(rbuf_t *handle, uint32_t index, const uint8_t *src, uint32_t n)
{
    uint32_t slot  = rbuf_slot(handle->count, index);
    uint32_t first = handle->count - slot;

    /* Up to the end of the memory first, then what is left from its start. */
    if (first > n)
    {
        first = n;
    }

    (void)memcpy(&handle->buffer[(size_t)slot * handle->item_size], src,
                 (size_t)first * handle->item_size);

    if (n > first)
    {
        (void)memcpy(handle->buffer, &src[(size_t)first * handle->item_size],
                     (size_t)(n - first) * handle->item_size);
    }
}

/*****************************************************************************************************/
/**
 * @brief Copy n items out of the slots from index on.
 *
 * @param[in]  handle  Handle to copy from.
 * @param[in]  index   Where the first item is, the start of the items a pop took.
 * @param[out] dst     Where the items go.
 * @param[in]  n       Items to copy, at most as many as there are.
 */
static void rbuf_copy_out(const rbuf_t *handle, uint32_t index, uint8_t *dst, uint32_t n)
{
    uint32_t slot  = rbuf_slot(handle->count, index);
    uint32_t first = handle->count - slot;

    /* Up to the end of the memory first, then what is left from its start. */
    if (first > n)
    {
        first = n;
    }

    (void)memcpy(dst, &handle->buffer[(size_t)slot * handle->item_size],
                 (size_t)first * handle->item_size);

    if (n > first)
    {
        (void)memcpy(&dst[(size_t)first * handle->item_size], handle->buffer,
                     (size_t)(n - first) * handle->item_size);
    }
}
