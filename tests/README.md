# Unit Tests for drmdevice

This directory contains unit tests for the drmdevice VDR plugin using the Catch2 testing framework.

The tests cover the parts of the plugin, which can be tested without any hardware (no DRM, no ALSA):

| Test file                 | Unit under test                                         |
| ------------------------- | ------------------------------------------------------- |
| `test_audioprocessor.cpp` | software volume, normalizer, compressor and equalizer   |
| `test_config.cpp`         | parsing of the setup.conf parameters                    |
| `test_filllevel.cpp`      | low pass filter of the audio buffer fill level          |
| `test_h264parser.cpp`     | H.264 SPS/PPS/slice header parser                       |
| `test_mediaresume.cpp`    | resume positions of the mediaplayer                     |
| `test_misc.cpp`           | helper functions of misc.h                              |
| `test_pes.cpp`            | PES parser and the audio/video reassembly buffers       |
| `test_pidcontroller.cpp`  | PID controller of the clock drift compensation          |
| `test_queue.cpp`          | thread-safe queue                                       |
| `test_ringbuffer.cpp`     | audio ring buffer                                       |

Every `test_*.cpp` in this directory is built automatically. Regression tests of fixed bugs
are kept in `test_*_regressions.cpp`. `h264streamwriter.h` composes H.264 NAL units for the
H.264 tests and `test_stubs.cpp` provides the few VDR symbols, which are needed to link the tests.

## Prerequisites

You need to have Catch2 v3 (since Ubuntu 24.04/Debian 13), valgrind and lcov installed on your system. On Debian/Ubuntu systems:

```bash
sudo apt-get install catch2 valgrind lcov
```

The tests expect the plugin to be located in the VDR source tree (`vdr/PLUGINS/src/drmdevice`).
Otherwise set the path to the VDR include directory:

```bash
make test VDRINCDIR=/path/to/vdr/include
```

## Building and Running Tests

Every build variant uses its own object directory (`obj/<variant>`), so the object files
of the plugin itself are never touched.

### Run tests

```bash
make test
```

This also writes a JUnit report to `test-results.xml`.

### Run tests with valgrind

```bash
make memcheck
```

The target fails on memory errors and definite memory leaks.

### Run tests with address and undefined behavior sanitizer

```bash
make sanitize
```

### Run specific tests

```bash
./obj/default/test_runner "[pes]"          # Run all PES tests
./obj/default/test_runner "cPes - MPEG2"   # Run MPEG2-related tests
```

### List all available tests

```bash
./obj/default/test_runner --list-tests
```

## Code Coverage

### Generate coverage report

```bash
make coverage
```

This will:
1. Build the tests with coverage instrumentation
2. Run all tests
3. Generate a coverage report showing which lines of code were executed

### Generate HTML coverage report

```bash
make coverage-html
```

This generates an interactive HTML report in `coverage-html/index.html` that you can open in a browser to see detailed coverage information with color-coded source files.
