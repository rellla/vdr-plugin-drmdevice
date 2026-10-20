#
# Makefile for the unit tests using Catch2 v3
#
# Build variants (selected with BUILD=...):
#   default   - plain debug build
#   sanitize  - build with AddressSanitizer and UndefinedBehaviorSanitizer
#   coverage  - build with gcov instrumentation for lcov reports
#
# Every variant uses its own object directory (obj/<variant>), so the objects
# of the plugin itself in the parent directory are never touched.
#

# Path to the VDR include directory (the default fits a plugin located in
# vdr/PLUGINS/src/drmdevice)
VDRINCDIR ?= ../../../../include

BUILD ?= default

CXX ?= g++
CXXFLAGS ?= -g -O1

override CXXFLAGS += -std=c++17 -W -Wall -Wextra -Werror=format -I.. -isystem $(VDRINCDIR) -D_GNU_SOURCE
override CXXFLAGS += $(shell pkg-config --cflags libavcodec libavformat libavutil catch2-with-main)
override CXXFLAGS += -DTEST_DATA_DIR='"$(CURDIR)/data"'
LIBS = $(shell pkg-config --libs libavformat libavcodec libavutil catch2-with-main)

ifeq ($(BUILD),sanitize)
SANITIZE_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all
override CXXFLAGS += $(SANITIZE_FLAGS)
override LDFLAGS += $(SANITIZE_FLAGS)
else ifeq ($(BUILD),coverage)
override CXXFLAGS += --coverage -O0
override LDFLAGS += --coverage
else ifneq ($(BUILD),default)
$(error Unknown BUILD variant '$(BUILD)', use default, sanitize or coverage)
endif

# Test sources (in this directory)
# (every test_*.cpp, test_stubs.cpp provides the needed VDR symbols)
TEST_SOURCES = $(sort $(wildcard test_*.cpp))

# Plugin sources under test (in the parent directory)
SOURCES_UNDER_TEST = \
	audioprocessor.cpp \
	config.cpp \
	filllevel.cpp \
	h264parser.cpp \
	logger.cpp \
	mediaresume.cpp \
	pes.cpp \
	pidcontroller.cpp \
	ringbuffer.cpp \
	spdif.cpp

OBJDIR = obj/$(BUILD)
OBJS = $(addprefix $(OBJDIR)/, $(TEST_SOURCES:.cpp=.o) $(SOURCES_UNDER_TEST:.cpp=.o))
DEPS = $(OBJS:.o=.d)
TEST_EXEC = $(OBJDIR)/test_runner

vpath %.cpp . ..

# Main target
.PHONY: all
all: $(TEST_EXEC)

$(TEST_EXEC): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR):
	@mkdir -p $@

-include $(DEPS)

# Run the tests and write a JUnit report
.PHONY: test
test: $(TEST_EXEC)
	./$(TEST_EXEC) -r console -r junit::out=test-results.xml

# Run the tests with valgrind (fails on memory errors and definite leaks)
.PHONY: memcheck
memcheck: $(TEST_EXEC)
	valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=definite ./$(TEST_EXEC)

# Build and run the tests with address and undefined behavior sanitizer
.PHONY: sanitize
sanitize:
	@$(MAKE) --no-print-directory BUILD=sanitize test

# Coverage
LCOV_FLAGS = --quiet --ignore-errors unused,unused
PLUGINDIR = $(abspath ..)

.PHONY: coverage
coverage:
	@rm -f obj/coverage/*.gcda
	@$(MAKE) --no-print-directory BUILD=coverage test
	lcov $(LCOV_FLAGS) --capture --directory obj/coverage --output-file coverage.info
	lcov $(LCOV_FLAGS) --extract coverage.info '$(PLUGINDIR)/*' --output-file coverage.info
	lcov $(LCOV_FLAGS) --remove coverage.info '$(PLUGINDIR)/tests/*' --output-file coverage.info
	lcov --list coverage.info

.PHONY: coverage-html
coverage-html: coverage
	genhtml --quiet coverage.info --output-directory coverage-html
	@echo "Coverage report generated in coverage-html/index.html"

# Clean
.PHONY: clean
clean:
	@rm -rf obj test-results.xml coverage.info coverage-html

.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all              - Build the test executable (default)"
	@echo "  test             - Build and run the tests (JUnit report: test-results.xml)"
	@echo "  memcheck         - Build and run the tests with valgrind"
	@echo "  sanitize         - Build and run the tests with ASan and UBSan"
	@echo "  coverage         - Build with coverage, run the tests and print a coverage summary"
	@echo "  coverage-html    - Generate an HTML coverage report (includes coverage)"
	@echo "  clean            - Remove all build artifacts, test results and coverage data"
	@echo
	@echo "Variables:"
	@echo "  VDRINCDIR        - VDR include directory (default: $(VDRINCDIR))"
	@echo "  BUILD            - build variant: default, sanitize or coverage"
