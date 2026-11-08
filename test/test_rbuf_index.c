/**
 * @file        test_rbuf_index.c
 * @brief       Tests of rbuf's index arithmetic, up to the largest buffer it allows.
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
 * @note        rbuf.c is compiled into this file, so its static helpers can be
 *              called. A buffer of 0x7FFFFFFF items will not fit in a PC's
 *              memory, but its indexes do, and those are what can overflow.
 */

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include "../src/rbuf.c"
#include "unity.h"

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

#define COUNT_MAX           0x7FFFFFFFU

/*
 * ****************************************************************************************************
 * Global variables
 * ****************************************************************************************************
*/

/* What the stub main.h reaches out to. Nothing here pushes or pops. */
uint32_t rbuf_test_primask;
uint32_t rbuf_test_disables;

/*
 * ****************************************************************************************************
 * Stub hooks
 * ****************************************************************************************************
*/

void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    TEST_FAIL_MESSAGE("assert_param");
}

void rbuf_test_set_primask(uint32_t value)
{
    rbuf_test_primask = value;
}

void rbuf_test_hook(void)
{
}

void rbuf_test_barrier(void)
{
}

/*
 * ****************************************************************************************************
 * Tests
 * ****************************************************************************************************
*/

static void test_every_index_of_small_buffers(void)
{
    /* Against the plain % sums, which cannot overflow at these sizes. */
    for (uint32_t count = 1U; count <= 6U; count++)
    {
        uint32_t laps = 2U * count;

        for (uint32_t index = 0U; index < laps; index++)
        {
            TEST_ASSERT_EQUAL_UINT32(index % count, rbuf_slot(count, index));

            for (uint32_t n = 0U; n <= count; n++)
            {
                TEST_ASSERT_EQUAL_UINT32((index + n) % laps, rbuf_advance(count, index, n));
            }

            for (uint32_t tail = 0U; tail < laps; tail++)
            {
                uint32_t used = (index + laps - tail) % laps;

                TEST_ASSERT_EQUAL_UINT32((used > count) ? count : used,
                                         rbuf_used_of(count, index, tail));
            }
        }
    }
}

static void test_advance_near_the_top_of_32_bits(void)
{
    /* 2 * COUNT_MAX is 0xFFFFFFFE. index + n would pass 32 bits in each of
       these, and a sum that wrapped would land on the wrong index. */
    TEST_ASSERT_EQUAL_HEX32(0x7FFFFFFEU, rbuf_advance(COUNT_MAX, 0xFFFFFFFDU, COUNT_MAX));
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, rbuf_advance(COUNT_MAX, 0xFFFFFFFDU, 1U));
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, rbuf_advance(COUNT_MAX, COUNT_MAX, COUNT_MAX));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFDU, rbuf_advance(COUNT_MAX, 0x80000000U, 0x7FFFFFFDU));
    TEST_ASSERT_EQUAL_HEX32(0x7FFFFFFFU, rbuf_advance(COUNT_MAX, 0U, COUNT_MAX));
}

static void test_used_and_slot_near_the_top_of_32_bits(void)
{
    TEST_ASSERT_EQUAL_HEX32(2U, rbuf_used_of(COUNT_MAX, 1U, 0xFFFFFFFDU));
    TEST_ASSERT_EQUAL_HEX32(COUNT_MAX, rbuf_used_of(COUNT_MAX, 0x7FFFFFFEU, 0xFFFFFFFDU));
    TEST_ASSERT_EQUAL_HEX32(COUNT_MAX, rbuf_used_of(COUNT_MAX, 0xFFFFFFFDU, 0x7FFFFFFEU));
    TEST_ASSERT_EQUAL_HEX32(0U, rbuf_used_of(COUNT_MAX, 0xFFFFFFFDU, 0xFFFFFFFDU));

    TEST_ASSERT_EQUAL_HEX32(0x7FFFFFFEU, rbuf_slot(COUNT_MAX, 0xFFFFFFFDU));
    TEST_ASSERT_EQUAL_HEX32(0U, rbuf_slot(COUNT_MAX, COUNT_MAX));
    TEST_ASSERT_EQUAL_HEX32(0x7FFFFFFEU, rbuf_slot(COUNT_MAX, 0x7FFFFFFEU));
}

/*
 * ****************************************************************************************************
 * Runner
 * ****************************************************************************************************
*/

void setUp(void)
{
}

void tearDown(void)
{
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_every_index_of_small_buffers);
    RUN_TEST(test_advance_near_the_top_of_32_bits);
    RUN_TEST(test_used_and_slot_near_the_top_of_32_bits);

    return UNITY_END();
}
