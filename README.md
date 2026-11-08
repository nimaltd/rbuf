# 🔄 rbuf

[![CI](https://github.com/nimaltd/rbuf/actions/workflows/ci.yml/badge.svg)](https://github.com/nimaltd/rbuf/actions/workflows/ci.yml)
[![Stars](https://img.shields.io/github/stars/nimaltd/rbuf?style=social)](https://github.com/nimaltd/rbuf)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE.md)

A ring buffer for STM32, written in C. It holds items of any type, from single bytes to structs, in memory you give it.

Push from a UART interrupt and pop in the main loop, or pass messages between FreeRTOS threads, with no mutex, no heap and no RTOS needed. Any number of interrupts and threads may share one buffer, and there is nothing to set for it.

---

## ✨ What you get

- Items of any type: bytes, words or a struct. Every push and pop copies
  whole items
- Your own memory, a plain array. Nothing calls `malloc()`, and a buffer too
  big for the RAM fails at link time, not when the program runs
- Every slot holds an item. None is kept free to tell full from empty
- Any number of places may push and pop the same buffer at once, with nothing
  to configure: an interrupt and the main loop, two interrupts at different
  priorities, or five threads logging into one buffer
- The interrupts are paused only for the few sums that take slots and give
  them back, never while items are copied. Measured, the highest priority
  interrupt runs at most 25 CPU cycles later than with no rbuf at all,
  whether a call moves 1 byte or 1 KB
- The same on bare metal, FreeRTOS and ThreadX. It needs no RTOS and no other
  library
- `rbuf_push_n()` and `rbuf_pop_n()` move many items at once, all of them or
  none, in at most two copies
- No `%` anywhere, so a Cortex-M0, which has no divide instruction, calls no
  division routine
- Small: about 550 bytes of code at `-Os` on a Cortex-M33, and a 36 byte
  handle
- Unit tested on every commit, with interrupts landing in the middle of a push
  and of a pop, three deep, and on real threads running at once. Tried on a
  NUCLEO-H503RB as well: five ways of sharing one buffer between the main loop
  and two timer interrupts, at up to 250,000 interrupts a second, moved more
  than 11 million items in 15 seconds, none of them lost, torn or out of order

---

## 📁 Layout

```
src/    rbuf.h, rbuf.c
test/   host unit tests, run on a PC
```

Installed into a project, the code keeps its `src/` folder, with the README,
changelog and licence files around it. There is no `test/`: the section below
about the tests refers to this repository, not to an installed copy.

---

## ⚙️ Installing it

[libNIMA](https://github.com/nimaltd/libNIMA) copies the library into your project and adds it to your CMake, STM32CubeIDE, Keil, IAR or Makefile project for you. Your project file is backed up first.

Get it once per machine from [its releases page](https://github.com/nimaltd/libNIMA/releases/latest): the installer for Windows, macOS or Linux, which puts the `libnima` command on your PATH as well.

Then, from the root of your STM32 project:

```bash
libnima nimaltd/rbuf
```

rbuf needs no other library, so nothing else is installed with it.

### From a downloaded zip

Downloaded this repository with **Code**, **Download ZIP**? Give libNIMA the zip in place of `nimaltd/rbuf`, with no need to unpack it:

```bash
libnima D:/Downloads/rbuf-main.zip
```

Only the files the library needs are copied into your project, and the zip is left alone. An unpacked folder works the same way. [libNIMA's README](https://github.com/nimaltd/libNIMA#using-it) has every option, and how to install on a machine with no internet at all.

### Updating, and pinning a version

Run the same command again, and the code is replaced. rbuf has no settings of its own to keep.

By default you get the newest code on `main`. To hold a project on one release, add `--ref` with a tag, a branch or a commit:

```bash
libnima nimaltd/rbuf --ref v3.0.0
```

### Or copy the files in by hand

1. Copy `src/rbuf.h` into your project's `Core/Inc`
2. Copy `src/rbuf.c` into your project's `Core/Src`

### Or add the whole repository to a CMake build

If you keep this repository as a submodule rather than installing it:

```cmake
add_subdirectory(rbuf)
target_link_libraries(${CMAKE_PROJECT_NAME} nimaltd::rbuf)

# rbuf is a static library, so it does not inherit your application's include
# paths and defines, and rbuf.c needs main.h. A CubeMX project keeps them on
# the stm32cubemx target.
target_link_libraries(rbuf PRIVATE stm32cubemx)
```

The first `target_link_libraries` has no `PRIVATE` on purpose. CubeMX links your application without one, and CMake refuses to mix the two forms on one target.

`libnima` avoids all of this: it writes an INTERFACE target instead, whose sources compile as part of your own target and inherit everything it has.

---

## 🚀 Getting started

Bytes from a UART, received by interrupt one at a time, and handled in the main loop. Enable the UART and its global interrupt in CubeMX, then:

```c
#include "rbuf.h"

#define RX_SIZE     128U

static uint8_t rx_memory[RX_SIZE];
static rbuf_t  rx;
static uint8_t rx_byte;

int main(void)
{
    /* ... HAL init, MX_USART2_UART_Init() ... */

    rbuf_init(&rx, rx_memory, sizeof(uint8_t), RX_SIZE);
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1);

    while (1)
    {
        uint8_t byte;

        while (rbuf_pop(&rx, &byte) == RBUF_ERR_NONE)
        {
            /* ... handle byte ... */
        }
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        rbuf_push(&rx, &rx_byte);
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
    }
}
```

`rbuf_init()` is given the memory, the size of one item and how many items fit. Every call returns `RBUF_ERR_NONE` when it worked, which is 0, so check for it by name: `if (rbuf_pop(...))` would mean "if it failed". A push into a full buffer returns `RBUF_ERR_FULL` and the byte is lost, so make the buffer big enough, as [below](#how-big).

### Several items at once

`rbuf_pop_n()` takes many items in one call, and `rbuf_push_n()` puts many in. Both move all of them or none: asked for more than there is, they return an error and leave everything as it was. So ask for what is there:

```c
uint8_t  line[32];
uint32_t n = rbuf_used(&rx);

if (n > sizeof(line))
{
    n = sizeof(line);
}

rbuf_pop_n(&rx, line, n);
```

When one place pops this buffer, `rbuf_used()` can only grow until it pops again, so the `rbuf_pop_n()` right after it always finds the `n` items. With several places popping, another may take some first, and `rbuf_pop_n()` then returns `RBUF_ERR_EMPTY` and takes none.

### Messages, between two threads

An item can be a struct, and it is copied in and out whole:

```c
typedef struct
{
    uint16_t id;
    int32_t  value;
} reading_t;

static reading_t readings_memory[8];
static rbuf_t    readings;

/* Once, before either thread uses it. */
rbuf_init(&readings, readings_memory, sizeof(reading_t), 8U);

/* In the thread that measures. */
reading_t r = { .id = 3, .value = adc_value };
rbuf_push(&readings, &r);

/* In the thread that sends. */
reading_t out;
if (rbuf_pop(&readings, &out) == RBUF_ERR_NONE)
{
    /* ... send out ... */
}
```

The memory needs no alignment of its own: items go in and out with `memcpy()`.

### How big

Enough for everything that can arrive while the popping side is busy. At 115200 baud a UART brings about 12 bytes every millisecond, so a main loop that can be 10 ms late needs room for 120 bytes, and 128 covers it. Every slot holds an item, so a buffer of 128 holds 128.

---

## 🧵 Interrupts, threads and an RTOS

### Who may call what

Every call, from anywhere: any interrupt at any priority, any thread, the main loop, and as many of them at once as you like. Only `rbuf_init()` comes first, before anything else uses the buffer.

The one exception is the NMI, and a fault handler such as HardFault. They are the only ones the interrupts' switch, PRIMASK, does not hold off, so they must not use a buffer anything else uses.

`rbuf_used()` and `rbuf_space()` count what a pop or a push could take right now. Read from the only place that pops, `rbuf_used()` can only grow until it pops again, and read from the only place that pushes, `rbuf_space()` can only grow until it pushes again. With several places at it, either can change the moment after it is read, and `rbuf_pop_n()` and `rbuf_push_n()` are what decide: all of it or nothing.

### How several at once keep out of each other's way

A push takes its slots first, with the interrupts off for a few instructions: it checks there is room, moves the end of the taken slots past its own, and counts itself as copying. Then it copies its items in with the interrupts on, however long that takes. Last, again for a few instructions, it counts itself out, and the push that brings that count to zero says every slot taken so far is ready to pop. A pop works the same way from the other end.

So an interrupt that lands in the middle of a push and pushes too takes the slots after it, and copies while the push it landed in is still copying. Its items come out after that push's, in the order the slots were taken, once both are done.

Measured on a NUCLEO-H503RB at 250 MHz: a timer at the highest priority, firing 250,000 times a second, noted how late it ran while the main loop pushed and popped over and over. That is set against the same loop doing the same copies with plain `memcpy()`, and against a lock round the whole call, the way most ring buffers made safe for interrupts do it.

| One call moves | Time the call takes | Interrupt late by, plain `memcpy()` | Interrupt late by, rbuf | Interrupt late by, whole call locked |
|---|---|---|---|---|
| 1 byte | 105 cycles | 42 cycles | 43 cycles | 125 cycles |
| 64 bytes | 480 cycles | 18 cycles | 42 cycles | 501 cycles |
| 1 KB | 6257 cycles | 18 cycles | 43 cycles | 6263 cycles |

Each figure is the worst seen in one second of it. rbuf adds at most 25 cycles to what the same copies cost with no lock at all, whatever their size. At 72 MHz that is 0.35 us, where a lock round a whole 1 KB call adds 87 us. A UART at 921600 baud brings a byte every 11 us, so that lock would lose bytes from it, and rbuf does not.

### What a copy holds up

Items come out in the order their slots were taken. Items pushed by an interrupt that landed in the middle of another push reach a pop only once that push is done too. Between interrupts that is never longer than one copy: 480 cycles for 64 bytes, 2 us at 250 MHz.

With an RTOS, a low priority thread that loses the CPU in the middle of its copy holds up the items pushed after it until it runs again. Nothing is lost or torn, they only come out later. Pushing and then waking the thread that pops, as in the example below, still works, because the push that finishes last wakes it after every item is there. A thread deleted or suspended from outside in the middle of a push or a pop would hold the buffer up for good, so do not stop one that may be inside an rbuf call.

On a NUCLEO-H503RB, two timer interrupts pushed into one buffer for 3 seconds, the higher priority one landing in the middle of the other's pushes, while the main loop popped: all 1.6 million messages came out whole and in order. The same with one interrupt pushing and both the main loop and a second interrupt popping, and with the main loop and an interrupt pushing while a higher interrupt popped.

### Why there is no mutex

A mutex cannot be taken in an interrupt, and an interrupt is where a ring buffer is most often filled. So rbuf takes none, and needs no RTOS and no osal. Its lock is PRIMASK, the CPU's own switch for the interrupts, saved and put back as it was, so it works inside an interrupt, inside a thread and before the RTOS starts. It is held for a few instructions only, so nothing ever waits on it for long.

It is not FreeRTOS's `taskENTER_CRITICAL()`, which pauses only the interrupts up to `configMAX_SYSCALL_INTERRUPT_PRIORITY`. An interrupt above that may use rbuf too, which it could not with FreeRTOS's.

### With an RTOS

rbuf never waits. A pop with nothing there returns `RBUF_ERR_EMPTY` at once. A thread that should sleep until something arrives waits on the RTOS, and whoever pushes wakes it. With CMSIS-RTOS v2:

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        rbuf_push(&rx, &rx_byte);
        HAL_UART_Receive_IT(&huart2, &rx_byte, 1);
        osThreadFlagsSet(rxTaskHandle, 1U);
    }
}

void StartRxTask(void *argument)
{
    uint8_t byte;

    for (;;)
    {
        osThreadFlagsWait(1U, osFlagsWaitAny, osWaitForever);

        while (rbuf_pop(&rx, &byte) == RBUF_ERR_NONE)
        {
            /* ... handle byte ... */
        }
    }
}
```

The thread pops everything each time it wakes, so a flag set twice before it runs loses nothing. Unlike the RTOS's own calls, rbuf can be set up and used before the kernel starts, from `main()`.

### Two cores

rbuf is made and tested for one core. PRIMASK pauses only the core that sets it, so on a dual core STM32H7 nothing keeps the other core out.

---

## 🧰 API

| Function | What it does |
|---|---|
| `rbuf_err_t rbuf_init(rbuf_t *handle, void *buffer, uint32_t item_size, uint32_t count)` | Set up a handle on `buffer`, room for `count` items of `item_size` bytes, and empty it |
| `rbuf_err_t rbuf_push(rbuf_t *handle, const void *item)` | Copy one item in, at the end |
| `rbuf_err_t rbuf_push_n(rbuf_t *handle, const void *items, uint32_t n)` | Copy `n` items in, at the end, all of them or none |
| `rbuf_err_t rbuf_pop(rbuf_t *handle, void *item)` | Copy the oldest item out, and take it out of the buffer |
| `rbuf_err_t rbuf_pop_n(rbuf_t *handle, void *items, uint32_t n)` | Copy the `n` oldest items out, and take them out, all of them or none |
| `void rbuf_flush(rbuf_t *handle)` | Drop every item. The memory is left as it is |
| `uint32_t rbuf_used(const rbuf_t *handle)` | How many items are waiting to be popped. Items still being pushed are not counted yet |
| `uint32_t rbuf_space(const rbuf_t *handle)` | How many more items there is room for. Slots still being popped are not counted yet |
| `bool rbuf_is_empty(const rbuf_t *handle)` | Whether there is nothing to pop |
| `bool rbuf_is_full(const rbuf_t *handle)` | Whether there is no room for one more |

| Error | Means |
|---|---|
| `RBUF_ERR_NONE` | Done |
| `RBUF_ERR_INVALID` | `rbuf_init()` refused its arguments, or was never called on this handle |
| `RBUF_ERR_FULL` | Not room for all the items. Nothing was pushed |
| `RBUF_ERR_EMPTY` | Fewer items in it than asked for. Nothing was popped, and the place for them is left alone |

`rbuf_init()` refuses an item size or a count of 0, more than 0x7FFFFFFF items, and a buffer of more than 4 GB in all. A handle it refused, or one it never saw, is refused by every push and pop, and reads as both empty and full. `n` of 0 does nothing and returns `RBUF_ERR_NONE`.

A `NULL` pointer is a bug in the calling code, so it is not returned as an error: `assert_param()` stops at it, the way the HAL does. That needs **Enable Full Assert** in CubeMX (Project Manager, Code Generator), which defines `USE_FULL_ASSERT`. Turn it on while developing, and a `NULL` lands in `assert_failed()` with the file and line. Without it, nothing checks.

---

## 🧪 Running the tests

The tests run on your PC, not on hardware. You need cmake and any C compiler, nothing else: [Unity](https://github.com/ThrowTheSwitch/Unity) is vendored into `test/unity/`, so there is nothing to install.

One command does everything:

```bash
python test/run_tests.py
```

It configures, builds and runs the suite, then tells you plainly whether it passed. An interrupt is played at the worst moment, in the middle of a push and of a pop, after it took its slots and before it is done: one that pops, one that pushes, one that flushes, and pushes landing in each other three deep. Each checks what can be popped and what room there is at that moment, as well as what comes out after. A random run is checked against a simple model, item by item. Then real threads run at once on your PC's cores, their copies overlapping for real: one pushing and one popping two million items, then four pushing and three popping, and every item has to arrive once, whole and in order. Add `--clean` to start from an empty build folder.

If you prefer doing it by hand:

```bash
cmake -S . -B build -DRBUF_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## ⬆️ Coming from version 2

The CubeMX pack is replaced by libNIMA, and every name changed:

| Was | Is now |
|---|---|
| `RBUF_HandleTypeDef *rb = RBUF_Init(sizeof(uint8_t), 64)` | `static uint8_t mem[64]; static rbuf_t rb;` and `rbuf_init(&rb, mem, sizeof(uint8_t), 64)` |
| `RBUF_DeInit(rb)` | nothing: the memory is yours |
| `RBUF_Push(rb, &x)`, `RBUF_PushN(rb, x, n)` | `rbuf_push(&rb, &x)`, `rbuf_push_n(&rb, x, n)` |
| `RBUF_Pop(rb, &x)`, `RBUF_PopN(rb, x, n)` | `rbuf_pop(&rb, &x)`, `rbuf_pop_n(&rb, x, n)` |
| `RBUF_Used(rb)` | `rbuf_used(&rb)` |
| `RBUF_Available(rb)` | `rbuf_space(&rb)` |
| `RBUF_IsEmpty(rb)`, `RBUF_IsFull(rb)` | `rbuf_is_empty(&rb)`, `rbuf_is_full(&rb)` |
| `RBUF_Format(rb)` | `rbuf_flush(&rb)` |

The heap no longer has to be made bigger: the memory is the array you pass.

One to watch, because the compiler finds every renamed function for you, but not this. The functions used to return `true` when they worked, and now return `RBUF_ERR_NONE`, which is 0:

```c
if (RBUF_Pop(rb, &x))                       /* was */
if (rbuf_pop(&rb, &x) == RBUF_ERR_NONE)     /* is now */
if (rbuf_pop(&rb, &x))                      /* compiles, and means "if it failed" */
```

Version 2 was not safe to push from an interrupt while the main loop popped: both changed the same count, and items could be lost. Version 3 is, and any number of places may push and pop the same buffer at once.

---

## 🤝 Contributing

Bug reports and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for the style rules and how to run the tests. Nothing to sign, just open a pull request.

---

## 💖 Support

I write these libraries in my own time and give them away, because good tools should be easy to get. If this one saved you an afternoon, there are two things that genuinely help:

**⭐ Star the repo.** It costs you one click, it helps other engineers find the library, and it is the main reason I keep going.

**☕ [Buy me a coffee on Ko-fi](https://ko-fi.com/nimaltd).** Any amount is a real motivation to keep writing, documenting and maintaining this work.

[![GitHub](https://img.shields.io/badge/GitHub-Follow-black?style=for-the-badge&logo=github)](https://github.com/NimaLTD)
[![YouTube](https://img.shields.io/badge/YouTube-Subscribe-red?style=for-the-badge&logo=youtube)](https://youtube.com/@nimaltd)
[![Instagram](https://img.shields.io/badge/Instagram-Follow-purple?style=for-the-badge&logo=instagram)](https://instagram.com/github.nimaltd)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-Connect-blue?style=for-the-badge&logo=linkedin)](https://linkedin.com/in/nimaltd)
[![Email](https://img.shields.io/badge/Email-Contact-red?style=for-the-badge&logo=gmail)](mailto:nima.askari@gmail.com)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-orange?style=for-the-badge&logo=ko-fi)](https://ko-fi.com/nimaltd)

---

## 📜 License

Apache License 2.0. See [LICENSE.md](LICENSE.md).

You are free to use this in commercial and closed source products. What the license asks in return is that you keep the copyright notice and pass along the [NOTICE](NOTICE) file, so the credit travels with the code.

The test folder vendors [Unity](https://github.com/ThrowTheSwitch/Unity) under its own MIT license, kept in [test/unity/LICENSE.txt](test/unity/LICENSE.txt). It is only used for testing and is not part of what you flash to a device.
