# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.0] - 2026-11-08

A rewrite. Code written for version 2 will not compile until it is updated.

### Changed

- **Every public name changed**, to match the other NimaLTD libraries:

  | Was | Is now |
  |---|---|
  | `rbuf.h`, `rbuf.c` at the top of the repository | `src/rbuf.h`, `src/rbuf.c` |
  | `RBUF_HandleTypeDef *rb = RBUF_Init(size, count)` | `rbuf_t rb;` and `rbuf_init(&rb, memory, size, count)` |
  | `RBUF_DeInit(rb)` | gone: the memory is yours |
  | `RBUF_Push(rb, &x)`, `RBUF_PushN(rb, x, n)` | `rbuf_push(&rb, &x)`, `rbuf_push_n(&rb, x, n)` |
  | `RBUF_Pop(rb, &x)`, `RBUF_PopN(rb, x, n)` | `rbuf_pop(&rb, &x)`, `rbuf_pop_n(&rb, x, n)` |
  | `RBUF_Used(rb)` | `rbuf_used(&rb)` |
  | `RBUF_Available(rb)` | `rbuf_space(&rb)` |
  | `RBUF_IsEmpty(rb)`, `RBUF_IsFull(rb)` | `rbuf_is_empty(&rb)`, `rbuf_is_full(&rb)` |
  | `RBUF_Format(rb)` | `rbuf_flush(&rb)`, which leaves the memory as it is |

- **The memory is yours, not the heap's.** `rbuf_init()` takes an array you
  declare, so nothing calls `malloc()`, the heap needs no room, and a buffer
  too big for the RAM fails at link time instead of when the program runs.
  Version 2 took it with `malloc()`, and CubeMX's default heap of 512 bytes
  made `RBUF_Init(1, 1024)` return `NULL`.
- **Push, pop and the rest return `rbuf_err_t` instead of `bool`**, and
  success is `RBUF_ERR_NONE`, which is 0. So `if (RBUF_Pop(rb, &x))` becomes
  `if (rbuf_pop(&rb, &x) == RBUF_ERR_NONE)`. A plain rename to
  `if (rbuf_pop(&rb, &x))` still compiles and means the opposite, so check
  every call.
- `rbuf_push_n()` and `rbuf_pop_n()` copy in at most two pieces, one each side
  of the end of the memory, rather than one item at a time.
- The indexes use no `%`, which a Cortex-M0 has no instruction for, so a push
  or a pop there no longer calls the compiler's division routine.
- Installed with [libNIMA](https://github.com/nimaltd/libNIMA) rather than the
  STM32CubeMX pack.

### Added

- Any number of interrupts and threads may push and pop one buffer at once,
  with nothing to configure. A push or a pop takes its slots with the
  interrupts off for a few instructions, copies with them on, and the last one
  to finish says every slot taken so far is done. Measured on an STM32H503,
  the highest priority interrupt runs at most 25 cycles later than with no
  rbuf at all, for a 1 byte call or a 1 KB one. It needs no RTOS, and works
  the same with FreeRTOS or ThreadX.
- A NULL pointer is caught by `assert_param()`, as the HAL does.
- Host unit tests, run on every commit, with interrupts played in the middle
  of a push and a pop, three deep, and stress tests on real threads.

### Fixed

- Pushing from an interrupt while the main loop popped could lose items or
  make up ones that were never pushed. Both changed the same `Count`, and an
  interrupt landing between its read and its write undid the other side's
  change. Now a push and a pop never write the same field.
- A loop waiting on `RBUF_IsEmpty()` could spin for ever with optimisation on,
  since nothing told the compiler another side changes the buffer. The
  indexes are `__IO` now.
- The README example passed `&rbuf`, a pointer to the pointer `RBUF_Init()`
  returned, to `RBUF_Push()` and `RBUF_Pop()`.

## [2.2.0]

- Added `RBUF_PushN()` and `RBUF_PopN()`.

## [2.1.0]

- Added `RBUF_Used()` and `RBUF_Format()`.

## [2.0.0]

- Rewritten, and installed through the STM32CubeMX pack.
