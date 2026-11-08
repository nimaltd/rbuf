# Contributing

Thanks for wanting to help. Bug reports, fixes and new features are all welcome.
There is no contributor agreement to sign: under section 5 of the Apache Licence
2.0, anything you submit for inclusion is covered by the project's own licence.

## Reporting a bug

Open an issue and include the STM32 family you are using, the RTOS if there
is one, which places push and pop the buffer (an interrupt, a thread, the main
loop), the error value you got back, and the smallest piece of code that shows
the problem. A failing test is even better, see below.

## Making a change

1. Add or update a test in `test/test_rbuf.c` that fails before your change and
   passes after it. An interrupt landing in the middle of a push or a pop is
   played from `RBUF_TEST_HOOK()`, so most races can be tested on a PC. If a
   change cannot be covered by a test, say why in the pull request.
2. Run the tests:

   ```bash
   python test/run_tests.py
   ```

3. Match the existing code style. The short version: 4 spaces and no tabs, Allman
   braces, `snake_case`, every file scope name prefixed with `rbuf_`, a Doxygen
   block on every public function, section banners at 103 columns, and comments
   in plain 7-bit ASCII with no em dashes. A `.clang-format` in the project root
   handles the mechanical parts:

   ```bash
   clang-format -i src/rbuf.c src/rbuf.h
   ```

4. Keep the public header free of vendor headers. `src/rbuf.h` includes only
   `<stdbool.h>` and `<stdint.h>`, which is what lets the tests run on a PC.
   Anything from the HAL or CMSIS belongs in `src/rbuf.c`.

## What CI checks

Every pull request runs two jobs, and both must be green:

- the full test suite on Ubuntu, built with `-Wall -Wextra -Wpedantic -Werror`
- a cross compile of `src/rbuf.c` for Cortex-M0+, M4, M7 and M33, with the
  same warning settings

Warnings are errors, so a build that warns will not merge.

## Questions

Open an issue, or reach me at nima.askari@gmail.com.
