# Unit Tests for drmdevice

This directory contains unit tests for the drmdevice VDR plugin using the Catch2 testing framework.

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
