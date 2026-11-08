/**
 * @file        test_rbuf.c
 * @brief       Host unit tests for the rbuf library.
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
 * @note        An interrupt is played from the hook the library calls while a push
 *              or a pop copies, after it took its slots and before it says it is
 *              done. Raised while PRIMASK is set, it waits, as a real one does.
 */

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include "rbuf.h"

#include <setjmp.h>
#include <stdbool.h>
#include <string.h>

#include "main.h"
#include "unity.h"

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* Compares two rbuf_err_t values, printing both as numbers when they differ. */
#define ASSERT_ERR(expected, call) \
    TEST_ASSERT_EQUAL_INT((int)(expected), (int)(call))

/* Runs one call that must stop at an assert_param, and checks that it did. */
#define EXPECT_ASSERT(call)                              \
    do                                                   \
    {                                                    \
        assert_expected = true;                          \
        assert_hits     = 0U;                            \
        if (setjmp(assert_return) == 0)                  \
        {                                                \
            (void)(call);                                \
        }                                                \
        assert_expected = false;                         \
        TEST_ASSERT_EQUAL_UINT32(1U, assert_hits);       \
    }                                                    \
    while (0)

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief An item with padding in it, as a message between two tasks would be.
 */
typedef struct
{
    uint32_t id;
    uint16_t value;
    uint8_t  flag;

} test_msg_t;

/*
 * ****************************************************************************************************
 * Global variables
 * ****************************************************************************************************
*/

/* What the stub main.h reaches out to. */
uint32_t rbuf_test_primask;
uint32_t rbuf_test_disables;

/* The buffer under test, and memory for it in the two shapes the tests use. */
static rbuf_t   ring;
static uint32_t mem32[16];
static uint8_t  mem8[256];

/* assert_param() lands in assert_failed(), which jumps back here. */
static jmp_buf  assert_return;
static bool     assert_expected;
static uint32_t assert_hits;

/* The interrupt the hook raises, and on which call of the hook. A test arms it
   for the next call with hook_calls + 1, since its own setup pushes count too. */
static void     (*irq_fn)(void);
static uint32_t irq_at;
static uint32_t hook_calls;
static bool     irq_pending;
static uint32_t irq_runs;

/* What the interrupt saw and did. */
static uint32_t   isr_value;
static rbuf_err_t isr_err;
static uint32_t   isr_popped[8];
static uint32_t   isr_pop_count;
static uint32_t   isr_head_seen;
static uint32_t   isr_tail_seen;
static uint32_t   isr_head_after;
static uint32_t   isr_tail_after;
static uint32_t   isr_used_after;
static uint32_t   isr_space_after;

/* For interrupts that land in each other, one deeper each time. */
static uint32_t   isr_depth;
static rbuf_err_t isr_errs[3];
static uint32_t   isr_heads[3];

/* Whether PRIMASK was ever set while the library copied. */
static uint32_t   hook_primask;

/* Every barrier, and what a test wants to look at when one comes. */
static uint32_t barrier_calls;
static void     (*barrier_fn)(void);
static uint32_t seen_index[4];
static uint32_t seen_value[4];
static uint32_t pop_out;

/* For the model test. */
static uint32_t random_state;

/*
 * ****************************************************************************************************
 * Stub hooks
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Where assert_param lands. Jumps back to the test that expected it.
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;

    assert_hits++;

    if (!assert_expected)
    {
        TEST_FAIL_MESSAGE("assert_param stopped a call that should have gone through");
    }

    longjmp(assert_return, 1);
}

/*****************************************************************************************************/
/**
 * @brief Raise the test's interrupt: it runs now, or once PRIMASK is cleared.
 */
static void irq_raise(void)
{
    if (rbuf_test_primask == 0U)
    {
        irq_runs++;
        irq_fn();
    }
    else
    {
        irq_pending = true;
    }
}

/*****************************************************************************************************/
/**
 * @brief Called by the library between copying the items and saying they are there.
 */
void rbuf_test_hook(void)
{
    hook_calls++;
    hook_primask |= rbuf_test_primask;

    if ((irq_fn != NULL) && (hook_calls == irq_at))
    {
        irq_raise();
    }
}

/*****************************************************************************************************/
/**
 * @brief Set PRIMASK. Clearing it lets a pending interrupt in, as on the hardware.
 */
void rbuf_test_set_primask(uint32_t value)
{
    rbuf_test_primask = value;

    if ((value == 0U) && irq_pending)
    {
        irq_pending = false;
        irq_runs++;
        irq_fn();
    }
}

/*****************************************************************************************************/
/**
 * @brief Called for every __DMB().
 */
void rbuf_test_barrier(void)
{
    barrier_calls++;

    if (barrier_fn != NULL)
    {
        barrier_fn();
    }
}

/*
 * ****************************************************************************************************
 * Helpers
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief What the interrupt sees once it has made its call.
 */
static void isr_look(void)
{
    isr_head_after  = ring.head;
    isr_tail_after  = ring.tail;
    isr_used_after  = rbuf_used(&ring);
    isr_space_after = rbuf_space(&ring);
}

/*****************************************************************************************************/
/**
 * @brief An interrupt that pushes isr_value.
 */
static void isr_push(void)
{
    isr_head_seen = ring.head;
    isr_err       = rbuf_push(&ring, &isr_value);
    isr_look();
}

/*****************************************************************************************************/
/**
 * @brief An interrupt that pops everything it can see.
 */
static void isr_pop_all(void)
{
    uint32_t value;

    isr_tail_seen = ring.tail;

    while (rbuf_pop(&ring, &value) == RBUF_ERR_NONE)
    {
        isr_popped[isr_pop_count] = value;
        isr_pop_count++;
    }

    isr_look();
}

/*****************************************************************************************************/
/**
 * @brief An interrupt that pops one item.
 */
static void isr_pop_one(void)
{
    isr_tail_seen = ring.tail;
    isr_err       = rbuf_pop(&ring, &isr_popped[0]);
    isr_look();
}

/*****************************************************************************************************/
/**
 * @brief An interrupt that drops everything.
 */
static void isr_flush(void)
{
    rbuf_flush(&ring);
    isr_look();
}

/*****************************************************************************************************/
/**
 * @brief An interrupt that pushes 10, 11 or 12, and is landed in by the next one twice.
 */
static void isr_push_deeper(void)
{
    uint32_t depth = isr_depth;
    uint32_t value = 10U + depth;

    isr_depth++;

    /* The next hook call is the one in the push just below, in the middle of
       copying this interrupt's own item. */
    if (isr_depth < 3U)
    {
        irq_at = hook_calls + 1U;
    }

    isr_errs[depth]  = rbuf_push(&ring, &value);
    isr_heads[depth] = ring.head;
}

/*****************************************************************************************************/
/**
 * @brief At a barrier in a push: where head is, and what is in the first slot.
 */
static void barrier_in_push(void)
{
    if (barrier_calls <= 4U)
    {
        seen_index[barrier_calls - 1U] = ring.head;
        seen_value[barrier_calls - 1U] = mem32[0];
    }
}

/*****************************************************************************************************/
/**
 * @brief At a barrier in a pop: where tail is, and what has been copied out.
 */
static void barrier_in_pop(void)
{
    if (barrier_calls <= 4U)
    {
        seen_index[barrier_calls - 1U] = ring.tail;
        seen_value[barrier_calls - 1U] = pop_out;
    }
}

/*****************************************************************************************************/
/**
 * @brief Set the ring up on mem32 for count items of four bytes.
 */
static void ring_of_words(uint32_t count)
{
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_init(&ring, mem32, sizeof(uint32_t), count));
}

/*****************************************************************************************************/
/**
 * @brief Push one word, which must go in.
 */
static void push_word(uint32_t value)
{
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push(&ring, &value));
}

/*****************************************************************************************************/
/**
 * @brief Pop one word, which must be there.
 */
static uint32_t pop_word(void)
{
    uint32_t value = 0U;

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop(&ring, &value));

    return value;
}

/*****************************************************************************************************/
/**
 * @brief A repeatable random number, xorshift32.
 */
static uint32_t random_next(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;

    return random_state;
}

/*****************************************************************************************************/
/**
 * @brief Put the indexes where a test wants them, with nothing being copied.
 */
static void set_indexes(uint32_t head, uint32_t tail)
{
    ring.head          = head;
    ring.head_reserved = head;
    ring.tail          = tail;
    ring.tail_reserved = tail;
}

/*****************************************************************************************************/
/**
 * @brief Check that no push or pop is left half done.
 */
static void check_settled(void)
{
    TEST_ASSERT_EQUAL_UINT32(0U, ring.pushing);
    TEST_ASSERT_EQUAL_UINT32(0U, ring.popping);
    TEST_ASSERT_EQUAL_UINT32(ring.head, ring.head_reserved);
    TEST_ASSERT_EQUAL_UINT32(ring.tail, ring.tail_reserved);
}

/*****************************************************************************************************/
/**
 * @brief Check used, space, is_empty and is_full against what the test expects.
 */
static void check_level(uint32_t count, uint32_t used)
{
    TEST_ASSERT_EQUAL_UINT32(used, rbuf_used(&ring));
    TEST_ASSERT_EQUAL_UINT32(count - used, rbuf_space(&ring));
    TEST_ASSERT_EQUAL(used == 0U, rbuf_is_empty(&ring));
    TEST_ASSERT_EQUAL(used == count, rbuf_is_full(&ring));
    check_settled();
}

/*
 * ****************************************************************************************************
 * Unity fixtures
 * ****************************************************************************************************
*/

void setUp(void)
{
    (void)memset(&ring, 0, sizeof(ring));
    (void)memset(mem32, 0, sizeof(mem32));
    (void)memset(mem8, 0, sizeof(mem8));

    rbuf_test_primask  = 0U;
    rbuf_test_disables = 0U;
    assert_expected    = false;
    assert_hits        = 0U;
    irq_fn             = NULL;
    irq_at             = 1U;
    hook_calls         = 0U;
    irq_pending        = false;
    irq_runs           = 0U;
    isr_value          = 0U;
    isr_err            = RBUF_ERR_INVALID;
    isr_pop_count      = 0U;
    isr_head_seen      = 0xFFFFFFFFU;
    isr_tail_seen      = 0xFFFFFFFFU;
    isr_head_after     = 0xFFFFFFFFU;
    isr_tail_after     = 0xFFFFFFFFU;
    isr_used_after     = 0xFFFFFFFFU;
    isr_space_after    = 0xFFFFFFFFU;
    isr_depth          = 0U;
    hook_primask       = 0U;
    barrier_calls      = 0U;
    barrier_fn         = NULL;
    pop_out            = 0U;
    random_state       = 0x2545F491U;

    (void)memset(isr_popped, 0, sizeof(isr_popped));
    (void)memset(isr_heads, 0xFF, sizeof(isr_heads));

    for (uint32_t i = 0U; i < 3U; i++)
    {
        isr_errs[i] = RBUF_ERR_INVALID;
    }
    (void)memset(seen_index, 0xFF, sizeof(seen_index));
    (void)memset(seen_value, 0xFF, sizeof(seen_value));
}

void tearDown(void)
{
    /* Every call puts the interrupts back as it found them, and no interrupt
       is left waiting for a PRIMASK that was never cleared. */
    TEST_ASSERT_EQUAL_UINT32(0U, rbuf_test_primask);
    TEST_ASSERT_FALSE(irq_pending);
}

/*
 * ****************************************************************************************************
 * Tests: rbuf_init()
 * ****************************************************************************************************
*/

static void test_init_sets_up_an_empty_buffer(void)
{
    ring_of_words(5U);

    check_level(5U, 0U);
    TEST_ASSERT_EQUAL_PTR(mem32, ring.buffer);
    TEST_ASSERT_EQUAL_UINT32(4U, ring.item_size);
}

static void test_init_refuses_a_size_of_zero(void)
{
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_init(&ring, mem8, 0U, 8U));
    TEST_ASSERT_EQUAL_UINT32(0U, ring.count);

    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_init(&ring, mem8, 1U, 0U));
    TEST_ASSERT_EQUAL_UINT32(0U, ring.count);
}

static void test_init_takes_up_to_0x7fffffff_items(void)
{
    /* Nothing is pushed, so the small memory is never reached. */
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_init(&ring, mem8, 1U, 0x7FFFFFFFU));
    TEST_ASSERT_EQUAL_UINT32(0x7FFFFFFFU, rbuf_space(&ring));

    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_init(&ring, mem8, 1U, 0x80000000U));
    TEST_ASSERT_EQUAL_UINT32(0U, ring.count);

    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_init(&ring, mem8, 1U, 0xFFFFFFFFU));
    TEST_ASSERT_EQUAL_UINT32(0U, ring.count);
}

static void test_init_refuses_more_bytes_than_32_bits_hold(void)
{
    /* 3 * 0x55555555 is 0xFFFFFFFF exactly, the most there can be. */
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_init(&ring, mem8, 0x55555555U, 3U));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_init(&ring, mem8, 0x55555556U, 3U));
    TEST_ASSERT_EQUAL_UINT32(0U, ring.count);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_init(&ring, mem8, 0xFFFFU, 0x10000U));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_init(&ring, mem8, 0x10000U, 0x10000U));
    TEST_ASSERT_EQUAL_UINT32(0U, ring.count);
}

static void test_refused_init_refuses_every_call(void)
{
    uint32_t value = 7U;
    uint32_t out   = 0xDEADBEEFU;

    ring_of_words(4U);
    push_word(1U);

    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_init(&ring, mem32, 4U, 0U));

    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_push(&ring, &value));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_push_n(&ring, &value, 1U));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_pop(&ring, &out));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_pop_n(&ring, &out, 1U));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, out);
    TEST_ASSERT_EQUAL_UINT32(0U, rbuf_used(&ring));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuf_space(&ring));
    TEST_ASSERT_TRUE(rbuf_is_empty(&ring));
    TEST_ASSERT_TRUE(rbuf_is_full(&ring));

    /* A later init that works brings it back, empty. */
    ring_of_words(4U);
    check_level(4U, 0U);
    push_word(9U);
    TEST_ASSERT_EQUAL_UINT32(9U, pop_word());
}

static void test_handle_never_set_up_is_refused(void)
{
    uint32_t value = 7U;
    uint32_t out   = 0xDEADBEEFU;

    /* setUp() zeroes it, as a static handle starts. */
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_push(&ring, &value));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_pop(&ring, &out));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_push_n(&ring, &value, 0U));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_pop_n(&ring, &out, 0U));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, out);
    TEST_ASSERT_EQUAL_UINT32(0U, rbuf_used(&ring));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuf_space(&ring));

    rbuf_flush(&ring);
    TEST_ASSERT_EQUAL_UINT32(0U, ring.count);
}

static void test_init_again_empties_it(void)
{
    ring_of_words(4U);
    push_word(1U);
    push_word(2U);
    (void)pop_word();
    push_word(3U);
    ring.pushing = 5U;
    ring.popping = 6U;

    ring_of_words(4U);

    check_level(4U, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, ring.head);
    TEST_ASSERT_EQUAL_UINT32(0U, ring.tail);
}

/*
 * ****************************************************************************************************
 * Tests: one item at a time
 * ****************************************************************************************************
*/

static void test_pop_returns_what_was_pushed(void)
{
    ring_of_words(4U);

    push_word(0x12345678U);

    /* Into the first slot, and the rest left alone. */
    TEST_ASSERT_EQUAL_HEX32(0x12345678U, mem32[0]);
    TEST_ASSERT_EQUAL_HEX32(0U, mem32[1]);
    TEST_ASSERT_EQUAL_HEX32(0U, mem32[2]);
    TEST_ASSERT_EQUAL_HEX32(0U, mem32[3]);

    TEST_ASSERT_EQUAL_HEX32(0x12345678U, pop_word());
    check_level(4U, 0U);
}

static void test_items_come_out_in_the_order_they_went_in(void)
{
    ring_of_words(8U);

    for (uint32_t i = 0U; i < 5U; i++)
    {
        push_word(100U + i);
    }

    for (uint32_t i = 0U; i < 5U; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(100U + i, pop_word());
    }
}

static void test_every_slot_holds_an_item(void)
{
    uint32_t value = 99U;

    ring_of_words(4U);

    for (uint32_t i = 0U; i < 4U; i++)
    {
        push_word(i);
    }

    check_level(4U, 4U);
    ASSERT_ERR(RBUF_ERR_FULL, rbuf_push(&ring, &value));

    for (uint32_t i = 0U; i < 4U; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(i, pop_word());
    }

    check_level(4U, 0U);
}

static void test_push_to_a_full_buffer_changes_nothing(void)
{
    uint32_t before[4];
    uint32_t value = 0xAAAAAAAAU;

    ring_of_words(4U);

    for (uint32_t i = 0U; i < 4U; i++)
    {
        push_word(i + 1U);
    }

    (void)memcpy(before, mem32, sizeof(before));

    ASSERT_ERR(RBUF_ERR_FULL, rbuf_push(&ring, &value));

    TEST_ASSERT_EQUAL_MEMORY(before, mem32, sizeof(before));
    TEST_ASSERT_EQUAL_UINT32(4U, ring.head);
    TEST_ASSERT_EQUAL_UINT32(0U, ring.tail);
    check_settled();
}

static void test_pop_from_an_empty_buffer_leaves_the_item_alone(void)
{
    uint32_t out = 0xDEADBEEFU;

    ring_of_words(4U);

    ASSERT_ERR(RBUF_ERR_EMPTY, rbuf_pop(&ring, &out));

    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, out);
    TEST_ASSERT_EQUAL_UINT32(0U, ring.tail);

    /* And after the last one went, too. */
    push_word(1U);
    (void)pop_word();
    ASSERT_ERR(RBUF_ERR_EMPTY, rbuf_pop(&ring, &out));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, out);
}

static void test_used_and_space_at_every_level(void)
{
    ring_of_words(6U);

    for (uint32_t i = 0U; i < 6U; i++)
    {
        push_word(i);
        check_level(6U, i + 1U);
    }

    for (uint32_t i = 0U; i < 6U; i++)
    {
        (void)pop_word();
        check_level(6U, 5U - i);
    }
}

static void test_a_buffer_of_one_item(void)
{
    uint32_t value = 5U;

    ring_of_words(1U);

    for (uint32_t i = 0U; i < 5U; i++)
    {
        push_word(i);
        check_level(1U, 1U);
        ASSERT_ERR(RBUF_ERR_FULL, rbuf_push(&ring, &value));
        TEST_ASSERT_EQUAL_UINT32(i, pop_word());
        check_level(1U, 0U);
    }
}

static void test_items_of_three_bytes(void)
{
    uint8_t item[3];
    uint8_t out[3];

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_init(&ring, mem8, 3U, 5U));

    /* Round the memory several times, so items land on every slot. */
    for (uint32_t i = 0U; i < 40U; i++)
    {
        item[0] = (uint8_t)i;
        item[1] = (uint8_t)(i * 7U);
        item[2] = (uint8_t)(255U - i);

        ASSERT_ERR(RBUF_ERR_NONE, rbuf_push(&ring, item));

        if (i >= 2U)
        {
            ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop(&ring, out));
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(i - 2U), out[0]);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)((i - 2U) * 7U), out[1]);
            TEST_ASSERT_EQUAL_UINT8((uint8_t)(255U - (i - 2U)), out[2]);
        }
    }

    /* Nothing past the 15 bytes of five items was touched. */
    TEST_ASSERT_EQUAL_UINT8(0U, mem8[15]);
}

static void test_items_of_a_struct(void)
{
    test_msg_t msgs[3];
    test_msg_t in;
    test_msg_t out;

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_init(&ring, msgs, sizeof(test_msg_t), 3U));

    for (uint32_t i = 0U; i < 10U; i++)
    {
        (void)memset(&in, 0, sizeof(in));
        in.id    = 1000U + i;
        in.value = (uint16_t)(i * 3U);
        in.flag  = (uint8_t)(i & 1U);

        ASSERT_ERR(RBUF_ERR_NONE, rbuf_push(&ring, &in));
        ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop(&ring, &out));
        TEST_ASSERT_EQUAL_MEMORY(&in, &out, sizeof(in));
    }
}

static void test_indexes_go_twice_round_the_slots(void)
{
    bool second_lap = false;

    ring_of_words(3U);

    for (uint32_t i = 0U; i < 30U; i++)
    {
        push_word(i);
        TEST_ASSERT_LESS_THAN_UINT32(6U, ring.head);

        if (ring.head >= 3U)
        {
            second_lap = true;
        }

        TEST_ASSERT_EQUAL_UINT32(i, pop_word());
        TEST_ASSERT_LESS_THAN_UINT32(6U, ring.tail);
        check_level(3U, 0U);
    }

    TEST_ASSERT_TRUE(second_lap);
}

/*
 * ****************************************************************************************************
 * Tests: several items at a time
 * ****************************************************************************************************
*/

static void test_push_n_is_all_or_nothing(void)
{
    const uint32_t in[5] = { 11U, 12U, 13U, 14U, 15U };
    uint32_t       before[5];

    ring_of_words(5U);
    push_word(1U);
    push_word(2U);
    (void)memcpy(before, mem32, sizeof(before));

    ASSERT_ERR(RBUF_ERR_FULL, rbuf_push_n(&ring, in, 4U));
    TEST_ASSERT_EQUAL_MEMORY(before, mem32, sizeof(before));
    check_level(5U, 2U);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push_n(&ring, in, 3U));
    check_level(5U, 5U);

    TEST_ASSERT_EQUAL_UINT32(1U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(2U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(11U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(12U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(13U, pop_word());
}

static void test_pop_n_is_all_or_nothing(void)
{
    uint32_t out[4] = { 0xEEU, 0xEEU, 0xEEU, 0xEEU };

    ring_of_words(5U);
    push_word(1U);
    push_word(2U);
    push_word(3U);

    ASSERT_ERR(RBUF_ERR_EMPTY, rbuf_pop_n(&ring, out, 4U));
    TEST_ASSERT_EACH_EQUAL_UINT32(0xEEU, out, 4U);
    check_level(5U, 3U);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop_n(&ring, out, 3U));
    TEST_ASSERT_EQUAL_UINT32(1U, out[0]);
    TEST_ASSERT_EQUAL_UINT32(2U, out[1]);
    TEST_ASSERT_EQUAL_UINT32(3U, out[2]);
    TEST_ASSERT_EQUAL_UINT32(0xEEU, out[3]);
    check_level(5U, 0U);
}

static void test_zero_items_does_nothing(void)
{
    uint32_t value = 5U;
    uint32_t out   = 0xDEADBEEFU;

    ring_of_words(2U);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop_n(&ring, &out, 0U));
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push_n(&ring, &value, 0U));
    check_level(2U, 0U);

    push_word(1U);
    push_word(2U);
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push_n(&ring, &value, 0U));
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop_n(&ring, &out, 0U));
    check_level(2U, 2U);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, out);
    TEST_ASSERT_EQUAL_UINT32(1U, pop_word());
}

static void test_more_items_than_it_holds_is_refused(void)
{
    const uint32_t in[5] = { 1U, 2U, 3U, 4U, 5U };
    uint32_t       out[5];

    ring_of_words(4U);

    ASSERT_ERR(RBUF_ERR_FULL, rbuf_push_n(&ring, in, 5U));
    ASSERT_ERR(RBUF_ERR_FULL, rbuf_push_n(&ring, in, 0xFFFFFFFFU));
    check_level(4U, 0U);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push_n(&ring, in, 4U));
    ASSERT_ERR(RBUF_ERR_EMPTY, rbuf_pop_n(&ring, out, 5U));
    ASSERT_ERR(RBUF_ERR_EMPTY, rbuf_pop_n(&ring, out, 0xFFFFFFFFU));
    check_level(4U, 4U);
}

static void test_push_n_wraps_round_the_end_of_memory(void)
{
    const uint32_t in[4] = { 10U, 11U, 12U, 13U };
    uint32_t       out[4];

    ring_of_words(5U);

    for (uint32_t i = 0U; i < 3U; i++)
    {
        push_word(i);
        (void)pop_word();
    }

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push_n(&ring, in, 4U));

    /* Two at the end of the memory, two from its start. */
    TEST_ASSERT_EQUAL_UINT32(10U, mem32[3]);
    TEST_ASSERT_EQUAL_UINT32(11U, mem32[4]);
    TEST_ASSERT_EQUAL_UINT32(12U, mem32[0]);
    TEST_ASSERT_EQUAL_UINT32(13U, mem32[1]);
    TEST_ASSERT_EQUAL_UINT32(0U, mem32[5]);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop_n(&ring, out, 4U));
    TEST_ASSERT_EQUAL_MEMORY(in, out, sizeof(in));
}

static void test_pop_n_wraps_round_the_end_of_memory(void)
{
    const uint32_t in[3] = { 7U, 8U, 9U };
    uint32_t       out[4] = { 0U, 0U, 0U, 0xEEU };

    ring_of_words(5U);

    for (uint32_t i = 0U; i < 4U; i++)
    {
        push_word(i);
        (void)pop_word();
    }

    /* One at the last slot, two from the start, pushed one at a time so only
       the pop has to split its copy. */
    for (uint32_t i = 0U; i < 3U; i++)
    {
        push_word(in[i]);
    }

    TEST_ASSERT_EQUAL_UINT32(7U, mem32[4]);
    TEST_ASSERT_EQUAL_UINT32(8U, mem32[0]);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop_n(&ring, out, 3U));
    TEST_ASSERT_EQUAL_MEMORY(in, out, sizeof(in));
    TEST_ASSERT_EQUAL_UINT32(0xEEU, out[3]);
}

static void test_all_of_it_at_once_from_the_middle(void)
{
    const uint32_t in[5] = { 21U, 22U, 23U, 24U, 25U };
    uint32_t       out[5];

    ring_of_words(5U);
    push_word(1U);
    push_word(2U);
    (void)pop_word();
    (void)pop_word();

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push_n(&ring, in, 5U));
    check_level(5U, 5U);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop_n(&ring, out, 5U));
    TEST_ASSERT_EQUAL_MEMORY(in, out, sizeof(in));
    check_level(5U, 0U);
}

static void test_matches_a_model(void)
{
    static const uint32_t sizes[] = { 1U, 3U, 4U };

    for (uint32_t s = 0U; s < (sizeof(sizes) / sizeof(sizes[0])); s++)
    {
        uint32_t item_size = sizes[s];

        for (uint32_t count = 1U; count <= 9U; count++)
        {
            uint8_t  model[64];
            uint32_t model_used = 0U;
            uint8_t  next_byte  = 1U;

            ASSERT_ERR(RBUF_ERR_NONE, rbuf_init(&ring, mem8, item_size, count));

            for (uint32_t op = 0U; op < 3000U; op++)
            {
                uint32_t   r    = random_next();
                uint32_t   kind = r % 5U;
                uint32_t   n    = (r >> 8) % (count + 2U);
                uint8_t    io[64];
                rbuf_err_t want;

                /* 0 pushes one, 1 pushes n, 2 pops one, 3 pops n, 4 now and
                   then flushes. n runs past count, so a refusal is tried too. */
                if ((kind == 0U) || (kind == 2U))
                {
                    n = 1U;
                }

                if (kind <= 1U)
                {
                    for (uint32_t i = 0U; i < (n * item_size); i++)
                    {
                        io[i] = next_byte;
                        next_byte++;
                    }

                    want = ((model_used + n) <= count) ? RBUF_ERR_NONE : RBUF_ERR_FULL;
                    ASSERT_ERR(want, (kind == 0U) ? rbuf_push(&ring, io) : rbuf_push_n(&ring, io, n));

                    if (want == RBUF_ERR_NONE)
                    {
                        (void)memcpy(&model[model_used * item_size], io, n * item_size);
                        model_used += n;
                    }
                }
                else if (kind <= 3U)
                {
                    (void)memset(io, 0, sizeof(io));
                    want = (n <= model_used) ? RBUF_ERR_NONE : RBUF_ERR_EMPTY;
                    ASSERT_ERR(want, (kind == 2U) ? rbuf_pop(&ring, io) : rbuf_pop_n(&ring, io, n));

                    if ((want == RBUF_ERR_NONE) && (n > 0U))
                    {
                        TEST_ASSERT_EQUAL_MEMORY(model, io, n * item_size);
                        model_used -= n;
                        (void)memmove(model, &model[n * item_size], model_used * item_size);
                    }
                }
                else if (((r >> 20) % 4U) == 0U)
                {
                    rbuf_flush(&ring);
                    model_used = 0U;
                }
                else
                {
                    /* Only the level check below. */
                }

                check_level(count, model_used);
            }
        }
    }
}

/*
 * ****************************************************************************************************
 * Tests: rbuf_flush(), and reading from a third place
 * ****************************************************************************************************
*/

static void test_flush_drops_everything(void)
{
    uint32_t out = 0xDEADBEEFU;

    ring_of_words(4U);
    push_word(1U);
    push_word(2U);
    push_word(3U);
    (void)pop_word();

    rbuf_flush(&ring);

    /* tail moved up to head, and head stayed: the pushing side's indexes are
       not the flush's to touch. */
    TEST_ASSERT_EQUAL_UINT32(3U, ring.head);
    TEST_ASSERT_EQUAL_UINT32(3U, ring.tail);
    check_level(4U, 0U);
    ASSERT_ERR(RBUF_ERR_EMPTY, rbuf_pop(&ring, &out));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, out);

    /* And it works on from there, with all the room back. */
    for (uint32_t i = 0U; i < 4U; i++)
    {
        push_word(10U + i);
    }

    for (uint32_t i = 0U; i < 4U; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(10U + i, pop_word());
    }
}

static void test_flush_of_a_full_buffer_gives_all_the_room_back(void)
{
    uint32_t value = 1U;

    ring_of_words(3U);

    for (uint32_t i = 0U; i < 3U; i++)
    {
        push_word(i);
    }

    ASSERT_ERR(RBUF_ERR_FULL, rbuf_push(&ring, &value));

    rbuf_flush(&ring);

    check_level(3U, 0U);
    push_word(7U);
    TEST_ASSERT_EQUAL_UINT32(7U, pop_word());
}

static void test_used_is_never_more_than_it_holds(void)
{
    ring_of_words(4U);

    /* head and tail from two different moments, as a second core could read
       them. */
    set_indexes(2U, 3U);
    TEST_ASSERT_EQUAL_UINT32(4U, rbuf_used(&ring));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuf_space(&ring));

    set_indexes(7U, 0U);
    TEST_ASSERT_EQUAL_UINT32(4U, rbuf_used(&ring));

    /* Full for real, with and without head having wrapped. */
    set_indexes(5U, 1U);
    TEST_ASSERT_EQUAL_UINT32(4U, rbuf_used(&ring));

    set_indexes(1U, 5U);
    TEST_ASSERT_EQUAL_UINT32(4U, rbuf_used(&ring));

    set_indexes(0U, 7U);
    TEST_ASSERT_EQUAL_UINT32(1U, rbuf_used(&ring));
    TEST_ASSERT_EQUAL_UINT32(3U, rbuf_space(&ring));
}

static void test_used_and_space_count_from_their_own_ends(void)
{
    ring_of_words(8U);

    /* Items 2 to 5 are in. A pop has taken item 2 and is copying it out, and a
       push has taken slots 6 and 7 and is copying in. A pop could take 3 to 5
       now, and a push slots 0 and 1, which were never used. */
    ring.tail          = 2U;
    ring.tail_reserved = 3U;
    ring.head          = 6U;
    ring.head_reserved = 8U;

    TEST_ASSERT_EQUAL_UINT32(3U, rbuf_used(&ring));
    TEST_ASSERT_EQUAL_UINT32(2U, rbuf_space(&ring));
}

/*
 * ****************************************************************************************************
 * Tests: ordering in memory
 * ****************************************************************************************************
*/

static void test_push_puts_the_item_in_memory_before_head_moves(void)
{
    uint32_t value = 0x11223344U;

    ring_of_words(4U);
    barrier_fn = barrier_in_push;

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push(&ring, &value));

    TEST_ASSERT_EQUAL_UINT32(1U, barrier_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, seen_index[0]);
    TEST_ASSERT_EQUAL_HEX32(0x11223344U, seen_value[0]);
    TEST_ASSERT_EQUAL_UINT32(1U, ring.head);
}

static void test_pop_reads_head_before_the_item_and_copies_before_tail_moves(void)
{
    ring_of_words(4U);
    push_word(0x55U);
    barrier_calls = 0U;
    barrier_fn    = barrier_in_pop;

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop(&ring, &pop_out));

    /* The first barrier comes before the copy, the second after it, and tail
       is moved only after both. */
    TEST_ASSERT_EQUAL_UINT32(2U, barrier_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, seen_index[0]);
    TEST_ASSERT_EQUAL_HEX32(0U, seen_value[0]);
    TEST_ASSERT_EQUAL_UINT32(0U, seen_index[1]);
    TEST_ASSERT_EQUAL_HEX32(0x55U, seen_value[1]);
    TEST_ASSERT_EQUAL_UINT32(1U, ring.tail);
}

/*
 * ****************************************************************************************************
 * Tests: an interrupt in the middle
 * ****************************************************************************************************
*/

static void test_pop_in_an_interrupt_during_a_push_sees_only_what_is_there(void)
{
    ring_of_words(4U);
    push_word(1U);

    /* The interrupt lands once the new item is copied in, before the push has
       said it is done, and pops all it can. */
    irq_fn = isr_pop_all;
    irq_at = hook_calls + 1U;
    push_word(2U);

    TEST_ASSERT_EQUAL_UINT32(1U, irq_runs);
    TEST_ASSERT_EQUAL_UINT32(1U, isr_pop_count);
    TEST_ASSERT_EQUAL_UINT32(1U, isr_popped[0]);
    TEST_ASSERT_EQUAL_UINT32(0U, isr_used_after);

    /* The item being pushed was not seen half done, and is there now. */
    TEST_ASSERT_EQUAL_UINT32(2U, pop_word());
    check_level(4U, 0U);
}

static void test_push_in_an_interrupt_during_a_pop_cannot_overwrite_it(void)
{
    ring_of_words(2U);
    push_word(1U);
    push_word(2U);

    /* The buffer is full, and the interrupt lands once the oldest item is
       copied out, before the pop gives its slot back. */
    irq_fn    = isr_push;
    isr_value = 99U;
    irq_at    = hook_calls + 1U;
    TEST_ASSERT_EQUAL_UINT32(1U, pop_word());

    TEST_ASSERT_EQUAL_UINT32(1U, irq_runs);
    ASSERT_ERR(RBUF_ERR_FULL, isr_err);
    TEST_ASSERT_EQUAL_UINT32(0U, isr_space_after);

    /* Its slot is free now that the pop is over. */
    push_word(99U);
    TEST_ASSERT_EQUAL_UINT32(2U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(99U, pop_word());
    check_level(2U, 0U);
}

static void test_push_in_an_interrupt_during_a_push_takes_the_next_slot(void)
{
    ring_of_words(4U);

    /* The interrupt lands in the middle of the first push, and pushes too. */
    irq_fn    = isr_push;
    isr_value = 2U;
    irq_at    = hook_calls + 1U;
    push_word(1U);

    TEST_ASSERT_EQUAL_UINT32(1U, irq_runs);
    ASSERT_ERR(RBUF_ERR_NONE, isr_err);

    /* Once its push was done, neither item could be popped yet: the one it
       landed in was not done. Both slots were taken, though. */
    TEST_ASSERT_EQUAL_UINT32(0U, isr_head_seen);
    TEST_ASSERT_EQUAL_UINT32(0U, isr_head_after);
    TEST_ASSERT_EQUAL_UINT32(0U, isr_used_after);
    TEST_ASSERT_EQUAL_UINT32(2U, isr_space_after);

    /* Each in its own slot, in the order the slots were taken, and both there
       once the first push is done. */
    TEST_ASSERT_EQUAL_UINT32(1U, mem32[0]);
    TEST_ASSERT_EQUAL_UINT32(2U, mem32[1]);
    TEST_ASSERT_EQUAL_UINT32(2U, ring.head);
    check_level(4U, 2U);
    TEST_ASSERT_EQUAL_UINT32(1U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(2U, pop_word());
}

static void test_pushes_three_deep_arrive_in_the_order_they_took_slots(void)
{
    ring_of_words(5U);
    push_word(1U);

    /* One interrupt lands in the main loop's push, a higher one in that
       interrupt's push, and a third in the second's. */
    irq_fn = isr_push_deeper;
    irq_at = hook_calls + 1U;
    push_word(2U);

    TEST_ASSERT_EQUAL_UINT32(3U, irq_runs);

    for (uint32_t i = 0U; i < 3U; i++)
    {
        ASSERT_ERR(RBUF_ERR_NONE, isr_errs[i]);

        /* None of them could move head: the push each landed in was not done. */
        TEST_ASSERT_EQUAL_UINT32(1U, isr_heads[i]);
    }

    check_level(5U, 5U);
    TEST_ASSERT_EQUAL_UINT32(1U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(2U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(10U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(11U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(12U, pop_word());
}

static void test_push_in_an_interrupt_counts_the_slots_a_push_took(void)
{
    ring_of_words(2U);
    push_word(1U);

    /* The last slot is taken by the push the interrupt lands in. */
    irq_fn    = isr_push;
    isr_value = 3U;
    irq_at    = hook_calls + 1U;
    push_word(2U);

    TEST_ASSERT_EQUAL_UINT32(1U, irq_runs);
    ASSERT_ERR(RBUF_ERR_FULL, isr_err);

    TEST_ASSERT_EQUAL_UINT32(1U, pop_word());
    TEST_ASSERT_EQUAL_UINT32(2U, pop_word());
    check_level(2U, 0U);
}

static void test_pop_in_an_interrupt_during_a_pop_takes_the_next_item(void)
{
    ring_of_words(4U);
    push_word(1U);
    push_word(2U);
    push_word(3U);

    irq_fn = isr_pop_one;
    irq_at = hook_calls + 1U;
    TEST_ASSERT_EQUAL_UINT32(1U, pop_word());

    TEST_ASSERT_EQUAL_UINT32(1U, irq_runs);
    ASSERT_ERR(RBUF_ERR_NONE, isr_err);
    TEST_ASSERT_EQUAL_UINT32(2U, isr_popped[0]);

    /* Once its pop was done, item 3 was left to pop, but no slot was free
       yet: the pop it landed in was still reading item 1. */
    TEST_ASSERT_EQUAL_UINT32(0U, isr_tail_seen);
    TEST_ASSERT_EQUAL_UINT32(0U, isr_tail_after);
    TEST_ASSERT_EQUAL_UINT32(1U, isr_used_after);
    TEST_ASSERT_EQUAL_UINT32(1U, isr_space_after);

    TEST_ASSERT_EQUAL_UINT32(2U, ring.tail);
    check_level(4U, 1U);
    TEST_ASSERT_EQUAL_UINT32(3U, pop_word());
}

static void test_flush_in_an_interrupt_during_a_push_keeps_that_push(void)
{
    ring_of_words(4U);
    push_word(1U);
    push_word(2U);

    irq_fn = isr_flush;
    irq_at = hook_calls + 1U;
    push_word(3U);

    /* The two that were in went, and the one being pushed came after. */
    TEST_ASSERT_EQUAL_UINT32(1U, irq_runs);
    TEST_ASSERT_EQUAL_UINT32(0U, isr_used_after);
    TEST_ASSERT_EQUAL_UINT32(3U, isr_space_after);

    check_level(4U, 1U);
    TEST_ASSERT_EQUAL_UINT32(3U, pop_word());
    check_level(4U, 0U);
}

static void test_flush_in_an_interrupt_during_a_pop_frees_its_slots_after(void)
{
    ring_of_words(4U);
    push_word(1U);
    push_word(2U);
    push_word(3U);

    irq_fn = isr_flush;
    irq_at = hook_calls + 1U;
    TEST_ASSERT_EQUAL_UINT32(1U, pop_word());

    /* Nothing was left to pop, but the slot being read was not free yet. */
    TEST_ASSERT_EQUAL_UINT32(1U, irq_runs);
    TEST_ASSERT_EQUAL_UINT32(0U, isr_used_after);
    TEST_ASSERT_EQUAL_UINT32(1U, isr_space_after);
    TEST_ASSERT_EQUAL_UINT32(0U, isr_tail_after);

    /* The pop gave back every slot once it was done. */
    TEST_ASSERT_EQUAL_UINT32(3U, ring.tail);
    check_level(4U, 0U);
}

static void test_the_copy_runs_with_the_interrupts_on(void)
{
    uint32_t words[3] = { 1U, 2U, 3U };

    ring_of_words(4U);

    push_word(1U);
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push_n(&ring, words, 3U));
    (void)pop_word();
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop_n(&ring, words, 3U));

    TEST_ASSERT_EQUAL_UINT32(4U, hook_calls);
    TEST_ASSERT_EQUAL_UINT32(0U, hook_primask);
}

static void test_interrupts_are_off_only_to_take_and_give_back(void)
{
    uint32_t words[2] = { 1U, 2U };
    uint32_t value    = 3U;

    /* Twice for a push or a pop, to take the slots and to give them back, and
       once for the rest. */
    ring_of_words(2U);

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push(&ring, &value));
    TEST_ASSERT_EQUAL_UINT32(2U, rbuf_test_disables);
    ASSERT_ERR(RBUF_ERR_FULL, rbuf_push_n(&ring, words, 2U));
    TEST_ASSERT_EQUAL_UINT32(3U, rbuf_test_disables);
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push_n(&ring, words, 1U));
    TEST_ASSERT_EQUAL_UINT32(5U, rbuf_test_disables);
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop(&ring, &value));
    TEST_ASSERT_EQUAL_UINT32(7U, rbuf_test_disables);
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop_n(&ring, words, 1U));
    TEST_ASSERT_EQUAL_UINT32(9U, rbuf_test_disables);
    ASSERT_ERR(RBUF_ERR_EMPTY, rbuf_pop(&ring, &value));
    TEST_ASSERT_EQUAL_UINT32(10U, rbuf_test_disables);
    (void)rbuf_used(&ring);
    TEST_ASSERT_EQUAL_UINT32(11U, rbuf_test_disables);
    (void)rbuf_space(&ring);
    TEST_ASSERT_EQUAL_UINT32(12U, rbuf_test_disables);
    (void)rbuf_is_empty(&ring);
    TEST_ASSERT_EQUAL_UINT32(13U, rbuf_test_disables);
    (void)rbuf_is_full(&ring);
    TEST_ASSERT_EQUAL_UINT32(14U, rbuf_test_disables);
    rbuf_flush(&ring);
    TEST_ASSERT_EQUAL_UINT32(15U, rbuf_test_disables);

    /* A handle rbuf_init() refused is turned away before. */
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_init(&ring, mem32, 4U, 0U));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_push(&ring, &value));
    ASSERT_ERR(RBUF_ERR_INVALID, rbuf_pop(&ring, &value));
    TEST_ASSERT_EQUAL_UINT32(15U, rbuf_test_disables);
    TEST_ASSERT_EQUAL_UINT32(0U, rbuf_test_primask);
}

static void test_interrupts_already_off_are_left_off(void)
{
    uint32_t value = 3U;

    ring_of_words(4U);

    rbuf_test_primask = 1U;

    ASSERT_ERR(RBUF_ERR_NONE, rbuf_push(&ring, &value));
    TEST_ASSERT_EQUAL_UINT32(1U, rbuf_test_primask);
    ASSERT_ERR(RBUF_ERR_NONE, rbuf_pop(&ring, &value));
    TEST_ASSERT_EQUAL_UINT32(1U, rbuf_test_primask);
    ASSERT_ERR(RBUF_ERR_EMPTY, rbuf_pop(&ring, &value));
    TEST_ASSERT_EQUAL_UINT32(1U, rbuf_test_primask);
    rbuf_flush(&ring);
    TEST_ASSERT_EQUAL_UINT32(1U, rbuf_test_primask);
    (void)rbuf_used(&ring);
    (void)rbuf_space(&ring);
    TEST_ASSERT_EQUAL_UINT32(1U, rbuf_test_primask);

    rbuf_test_primask = 0U;
}

/*
 * ****************************************************************************************************
 * Tests: a NULL is the caller's bug
 * ****************************************************************************************************
*/

static void test_null_pointers_are_caught_by_assert(void)
{
    uint32_t value = 1U;
    rbuf_t   before;

    ring_of_words(4U);
    push_word(5U);
    (void)memcpy(&before, &ring, sizeof(before));
    rbuf_test_disables = 0U;

    EXPECT_ASSERT(rbuf_init(NULL, mem32, 4U, 4U));
    EXPECT_ASSERT(rbuf_init(&ring, NULL, 4U, 4U));
    EXPECT_ASSERT(rbuf_push(NULL, &value));
    EXPECT_ASSERT(rbuf_push(&ring, NULL));
    EXPECT_ASSERT(rbuf_push_n(NULL, &value, 1U));
    EXPECT_ASSERT(rbuf_push_n(&ring, NULL, 1U));
    EXPECT_ASSERT(rbuf_pop(NULL, &value));
    EXPECT_ASSERT(rbuf_pop(&ring, NULL));
    EXPECT_ASSERT(rbuf_pop_n(NULL, &value, 1U));
    EXPECT_ASSERT(rbuf_pop_n(&ring, NULL, 1U));
    EXPECT_ASSERT(rbuf_flush(NULL));
    EXPECT_ASSERT(rbuf_used(NULL));
    EXPECT_ASSERT(rbuf_space(NULL));
    EXPECT_ASSERT(rbuf_is_empty(NULL));
    EXPECT_ASSERT(rbuf_is_full(NULL));

    /* Each stopped before it touched the handle or the interrupts. */
    TEST_ASSERT_EQUAL_MEMORY(&before, &ring, sizeof(before));
    TEST_ASSERT_EQUAL_UINT32(0U, rbuf_test_disables);
    TEST_ASSERT_EQUAL_UINT32(5U, pop_word());
}

/*
 * ****************************************************************************************************
 * Runner
 * ****************************************************************************************************
*/

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_sets_up_an_empty_buffer);
    RUN_TEST(test_init_refuses_a_size_of_zero);
    RUN_TEST(test_init_takes_up_to_0x7fffffff_items);
    RUN_TEST(test_init_refuses_more_bytes_than_32_bits_hold);
    RUN_TEST(test_refused_init_refuses_every_call);
    RUN_TEST(test_handle_never_set_up_is_refused);
    RUN_TEST(test_init_again_empties_it);

    RUN_TEST(test_pop_returns_what_was_pushed);
    RUN_TEST(test_items_come_out_in_the_order_they_went_in);
    RUN_TEST(test_every_slot_holds_an_item);
    RUN_TEST(test_push_to_a_full_buffer_changes_nothing);
    RUN_TEST(test_pop_from_an_empty_buffer_leaves_the_item_alone);
    RUN_TEST(test_used_and_space_at_every_level);
    RUN_TEST(test_a_buffer_of_one_item);
    RUN_TEST(test_items_of_three_bytes);
    RUN_TEST(test_items_of_a_struct);
    RUN_TEST(test_indexes_go_twice_round_the_slots);

    RUN_TEST(test_push_n_is_all_or_nothing);
    RUN_TEST(test_pop_n_is_all_or_nothing);
    RUN_TEST(test_zero_items_does_nothing);
    RUN_TEST(test_more_items_than_it_holds_is_refused);
    RUN_TEST(test_push_n_wraps_round_the_end_of_memory);
    RUN_TEST(test_pop_n_wraps_round_the_end_of_memory);
    RUN_TEST(test_all_of_it_at_once_from_the_middle);
    RUN_TEST(test_matches_a_model);

    RUN_TEST(test_flush_drops_everything);
    RUN_TEST(test_flush_of_a_full_buffer_gives_all_the_room_back);
    RUN_TEST(test_used_is_never_more_than_it_holds);
    RUN_TEST(test_used_and_space_count_from_their_own_ends);

    RUN_TEST(test_push_puts_the_item_in_memory_before_head_moves);
    RUN_TEST(test_pop_reads_head_before_the_item_and_copies_before_tail_moves);

    RUN_TEST(test_pop_in_an_interrupt_during_a_push_sees_only_what_is_there);
    RUN_TEST(test_push_in_an_interrupt_during_a_pop_cannot_overwrite_it);
    RUN_TEST(test_push_in_an_interrupt_during_a_push_takes_the_next_slot);
    RUN_TEST(test_pushes_three_deep_arrive_in_the_order_they_took_slots);
    RUN_TEST(test_push_in_an_interrupt_counts_the_slots_a_push_took);
    RUN_TEST(test_pop_in_an_interrupt_during_a_pop_takes_the_next_item);
    RUN_TEST(test_flush_in_an_interrupt_during_a_push_keeps_that_push);
    RUN_TEST(test_flush_in_an_interrupt_during_a_pop_frees_its_slots_after);
    RUN_TEST(test_the_copy_runs_with_the_interrupts_on);
    RUN_TEST(test_interrupts_are_off_only_to_take_and_give_back);
    RUN_TEST(test_interrupts_already_off_are_left_off);

    RUN_TEST(test_null_pointers_are_caught_by_assert);

    return UNITY_END();
}
