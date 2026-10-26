# Building from source

Most people do not need this page: take a ready file from the [releases page](https://github.com/Sea-For-Friends/chart-grabber-cli/releases).

## What you need

- Debian 12 or 13, or a system with the same libraries
- Qt 6.4 or newer
- CMake
- a C++23 compiler

The Debian packages to install are listed in [tools/packages-debian1x.txt](tools/packages-debian1x.txt).

## Build

```
$ git clone https://github.com/Sea-For-Friends/chart-grabber-cli.git
$ cd chart-grabber-cli
$ cmake --preset release
$ cmake --build --preset release
```

The program is `build/release/cli/chart-grabber-cli`.

## Change the code

Read [CONTRIBUTING.md](CONTRIBUTING.md). It explains how to run the tests and what a change must pass.
