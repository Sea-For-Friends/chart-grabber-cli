# Contributing

Thank you for helping. Open an issue before a large change, so that we can agree on the direction first.

## Build and run the tests

```
$ cmake --preset debug
$ cmake --build --preset debug
$ ctest --preset debug
```

The packages you need are in [tools/packages-debian1x.txt](tools/packages-debian1x.txt). Qt 6.4 or newer, CMake 3.25 or
newer and a C++23 compiler are required.

## Run the checks before you send a change

```
$ tools/validate.sh
```

The script checks the format, builds, runs the tests and the hygiene rules, and prints one line per finding in the form
`file:line: RULE: message`. It ends with `PASS` or `FAIL`. In a clone of this repository it skips the steps that belong
to the maintainers' own process and says so, one line each.

Useful options:

- `--fast` skips clang-tidy.
- `--changed` runs clang-tidy only on the files you changed.
- `--asan` builds and runs the tests with AddressSanitizer and UBSan. Use it when you touch threads, memory or child
  processes.
- `CG_JOBS=4 tools/validate.sh` limits the number of parallel jobs.

## Code style

- Format every C++ file you add or change with `clang-format -i`. The style is in `.clang-format`.
- clang-tidy settings are in `.clang-tidy`. A change should add no new finding.
- Comments say why, not what. Keep a change small and do not refactor code you are not changing.

## Tests

- A test is a GoogleTest case in `tests/<module>/<name>_test.cpp`, added to the `CMakeLists.txt` of that folder.
- Every test must be able to fail. Remove or break the code it covers once and check that the test fails.
- Write the expected values as literals. Do not compute them with the code under test.
- Test data goes into `tests/fixtures/<area>/`, with a `README.md` that says where the data came from. Do not put
  absolute paths of your machine into code, tests or documents.
- Threads in tests are started with `std::latch` or `std::barrier`. Do not wait with sleeps or spin loops.

## Comments with `T-NNNN`, `D-NN` or `L-NN`

Some comments and documents carry ids such as `T-NNNN`. They refer to the maintainers' internal tracker and mean
nothing outside it. Do not add new ones; describe the reason in words.

## Licence of your contribution

This program is free software under the GPL-3.0-or-later (see [LICENSE](LICENSE)). By sending a change you confirm that
you wrote it, or have the right to send it, and that it may be distributed under that licence. Sign off every commit
with `git commit -s`; this adds a `Signed-off-by:` line with your name and e-mail address, as in the
[Developer Certificate of Origin](https://developercertificate.org/).

## Report a security problem

Do not open a public issue. Read [SECURITY.md](SECURITY.md).

Contact: 347869794+sea4friends@users.noreply.github.com
