/**
 * @file        test_rbuf_threads.c
 * @brief       Stress tests for the rbuf library, run by real threads at once.
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
 * @note        One thread pushes and one pops, then several do both at once. The
 *              threads run on several cores, so the copies really overlap.
 */

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include "rbuf.h"

#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#include "main.h"
#include "unity.h"

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* One pusher and one popper. A small, odd count, so the buffer is full and
   empty again all the time and the copies split round its end. */
#define ONE_COUNT           7U
#define ONE_ITEMS           2000000U

/* Several of each. */
#define MANY_COUNT          5U
#define MANY_PUSHERS        4U
#define MANY_POPPERS        3U
#define MANY_ITEMS          100000U

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief An item that shows a torn copy: check is always ~seq.
 */
typedef struct
{
    uint32_t pusher;
    uint32_t seq;
    uint32_t check;

} item_t;

/*
 * ****************************************************************************************************
 * Global variables
 * ****************************************************************************************************
*/

static rbuf_t ring;
static item_t ring_mem[ONE_COUNT];

/* PRIMASK as one shared lock, and whether this thread holds it. */
static pthread_mutex_t irq_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local uint32_t irq_off;

/* What the threads found, read once they have all stopped. */
static uint32_t errors_order;
static uint32_t errors_torn;
static uint32_t errors_refused;
static uint32_t errors_twice;
static uint32_t popped_total;

/* Every item of every pusher, set once it was popped. */
static uint8_t seen[MANY_PUSHERS][MANY_ITEMS];

/*
 * ****************************************************************************************************
 * Stub hooks
 * ****************************************************************************************************
*/

uint32_t rbuf_thread_get_primask(void)
{
    return irq_off;
}

void rbuf_thread_disable_irq(void)
{
    if (irq_off == 0U)
    {
        (void)pthread_mutex_lock(&irq_lock);
        irq_off = 1U;
    }
}

void rbuf_thread_set_primask(uint32_t value)
{
    if ((value == 0U) && (irq_off != 0U))
    {
        irq_off = 0U;
        (void)pthread_mutex_unlock(&irq_lock);
    }
    else if ((value != 0U) && (irq_off == 0U))
    {
        (void)pthread_mutex_lock(&irq_lock);
        irq_off = 1U;
    }
    else
    {
        /* Already as asked. */
    }
}

/*
 * ****************************************************************************************************
 * Helpers
 * ****************************************************************************************************
*/

static uint32_t random_next(uint32_t *state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;

    return *state;
}

static void count_error(uint32_t *counter)
{
    (void)__atomic_fetch_add(counter, 1U, __ATOMIC_RELAXED);
}

/*
 * ****************************************************************************************************
 * One pusher, one popper
 * ****************************************************************************************************
*/

/* The pusher asks for the room first and pushes no more than that. From its
   side the room can only grow, so the push must never be refused. */
static void *one_pusher(void *arg)
{
    uint32_t state = 0x9E3779B9U;
    uint32_t seq   = 0U;
    item_t   items[4];

    (void)arg;

    while (seq < ONE_ITEMS)
    {
        uint32_t n     = (random_next(&state) % 4U) + 1U;
        uint32_t space = rbuf_space(&ring);

        if (space == 0U)
        {
            continue;
        }

        if (n > space)
        {
            n = space;
        }

        if (n > (ONE_ITEMS - seq))
        {
            n = ONE_ITEMS - seq;
        }

        for (uint32_t i = 0U; i < n; i++)
        {
            items[i].pusher = 0U;
            items[i].seq    = seq + i;
            items[i].check  = ~(seq + i);
        }

        if (((n == 1U) ? rbuf_push(&ring, items) : rbuf_push_n(&ring, items, n)) != RBUF_ERR_NONE)
        {
            count_error(&errors_refused);
            continue;
        }

        seq += n;
    }

    return NULL;
}

/* The popper the same way round: from its side what is used can only grow. */
static void *one_popper(void *arg)
{
    uint32_t state = 0x7F4A7C15U;
    uint32_t next  = 0U;
    item_t   items[4];

    (void)arg;

    while (next < ONE_ITEMS)
    {
        uint32_t n    = (random_next(&state) % 4U) + 1U;
        uint32_t used = rbuf_used(&ring);

        if (used == 0U)
        {
            continue;
        }

        if (n > used)
        {
            n = used;
        }

        if (((n == 1U) ? rbuf_pop(&ring, items) : rbuf_pop_n(&ring, items, n)) != RBUF_ERR_NONE)
        {
            count_error(&errors_refused);
            continue;
        }

        for (uint32_t i = 0U; i < n; i++)
        {
            if (items[i].check != ~items[i].seq)
            {
                count_error(&errors_torn);
            }

            if (items[i].seq != next)
            {
                count_error(&errors_order);
            }

            next = items[i].seq + 1U;
        }
    }

    return NULL;
}

static void test_one_pusher_one_popper(void)
{
    pthread_t pusher;
    pthread_t popper;

    TEST_ASSERT_EQUAL_INT(RBUF_ERR_NONE, rbuf_init(&ring, ring_mem, sizeof(item_t), ONE_COUNT));

    TEST_ASSERT_EQUAL_INT(0, pthread_create(&popper, NULL, one_popper, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&pusher, NULL, one_pusher, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(pusher, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(popper, NULL));

    TEST_ASSERT_EQUAL_UINT32(0U, errors_refused);
    TEST_ASSERT_EQUAL_UINT32(0U, errors_torn);
    TEST_ASSERT_EQUAL_UINT32(0U, errors_order);
    TEST_ASSERT_TRUE(rbuf_is_empty(&ring));
}

/*
 * ****************************************************************************************************
 * Several of each
 * ****************************************************************************************************
*/

/* Each pusher pushes its own numbers in order, a few at a time. */
static void *many_pusher(void *arg)
{
    uint32_t id    = (uint32_t)(uintptr_t)arg;
    uint32_t state = 0x1234567U + (id * 0x9E3779B9U);
    uint32_t seq   = 0U;
    item_t   items[3];

    while (seq < MANY_ITEMS)
    {
        uint32_t n = (random_next(&state) % 3U) + 1U;

        if (n > (MANY_ITEMS - seq))
        {
            n = MANY_ITEMS - seq;
        }

        for (uint32_t i = 0U; i < n; i++)
        {
            items[i].pusher = id;
            items[i].seq    = seq + i;
            items[i].check  = ~(seq + i);
        }

        if (rbuf_push_n(&ring, items, n) == RBUF_ERR_NONE)
        {
            seq += n;
        }
    }

    return NULL;
}

/* Each popper sees part of every pusher's numbers, and each part must still be
   in order. Over all poppers, every number arrives exactly once. */
static void *many_popper(void *arg)
{
    uint32_t state = 0xABCDEFU + (uint32_t)(uintptr_t)arg;
    uint32_t last[MANY_PUSHERS];
    item_t   items[3];

    for (uint32_t p = 0U; p < MANY_PUSHERS; p++)
    {
        last[p] = 0xFFFFFFFFU;
    }

    while (__atomic_load_n(&popped_total, __ATOMIC_RELAXED) < (MANY_PUSHERS * MANY_ITEMS))
    {
        uint32_t n = (random_next(&state) % 3U) + 1U;

        if (rbuf_pop_n(&ring, items, n) != RBUF_ERR_NONE)
        {
            continue;
        }

        for (uint32_t i = 0U; i < n; i++)
        {
            uint32_t p = items[i].pusher;
            uint32_t s = items[i].seq;

            if ((p >= MANY_PUSHERS) || (s >= MANY_ITEMS) || (items[i].check != ~s))
            {
                count_error(&errors_torn);
                continue;
            }

            if ((last[p] != 0xFFFFFFFFU) && (s <= last[p]))
            {
                count_error(&errors_order);
            }

            last[p] = s;

            if (__atomic_exchange_n(&seen[p][s], 1U, __ATOMIC_RELAXED) != 0U)
            {
                count_error(&errors_twice);
            }
        }

        (void)__atomic_fetch_add(&popped_total, n, __ATOMIC_RELAXED);
    }

    return NULL;
}

static void test_many_pushers_many_poppers(void)
{
    pthread_t pushers[MANY_PUSHERS];
    pthread_t poppers[MANY_POPPERS];
    uint32_t  missing = 0U;

    TEST_ASSERT_EQUAL_INT(RBUF_ERR_NONE, rbuf_init(&ring, ring_mem, sizeof(item_t), MANY_COUNT));

    for (uint32_t i = 0U; i < MANY_POPPERS; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&poppers[i], NULL, many_popper, (void *)(uintptr_t)i));
    }

    for (uint32_t i = 0U; i < MANY_PUSHERS; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&pushers[i], NULL, many_pusher, (void *)(uintptr_t)i));
    }

    for (uint32_t i = 0U; i < MANY_PUSHERS; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(pushers[i], NULL));
    }

    for (uint32_t i = 0U; i < MANY_POPPERS; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, pthread_join(poppers[i], NULL));
    }

    for (uint32_t p = 0U; p < MANY_PUSHERS; p++)
    {
        for (uint32_t s = 0U; s < MANY_ITEMS; s++)
        {
            missing += (seen[p][s] == 0U) ? 1U : 0U;
        }
    }

    TEST_ASSERT_EQUAL_UINT32(0U, errors_torn);
    TEST_ASSERT_EQUAL_UINT32(0U, errors_order);
    TEST_ASSERT_EQUAL_UINT32(0U, errors_twice);
    TEST_ASSERT_EQUAL_UINT32(0U, missing);
    TEST_ASSERT_EQUAL_UINT32(MANY_PUSHERS * MANY_ITEMS, popped_total);
    TEST_ASSERT_TRUE(rbuf_is_empty(&ring));
    TEST_ASSERT_EQUAL_UINT32(0U, ring.pushing);
    TEST_ASSERT_EQUAL_UINT32(0U, ring.popping);
}

/*
 * ****************************************************************************************************
 * Runner
 * ****************************************************************************************************
*/

void setUp(void)
{
    errors_order   = 0U;
    errors_torn    = 0U;
    errors_refused = 0U;
    errors_twice   = 0U;
    popped_total   = 0U;

    (void)memset(&ring, 0, sizeof(ring));
    (void)memset(seen, 0, sizeof(seen));
}

void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_one_pusher_one_popper);
    RUN_TEST(test_many_pushers_many_poppers);

    return UNITY_END();
}
