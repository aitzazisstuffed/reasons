# Makefile for Reasons Decision Tree Debugger
# Traditional make build system with comprehensive feature support

# Project information
PACKAGE_NAME = reasons
VERSION = 0.1.0
DESCRIPTION = Decision Tree Debugger and DSL
HOMEPAGE = https://github.com/yourusername/reasons

# Parse version components
VERSION_MAJOR = $(shell echo $(VERSION) | cut -d. -f1)
VERSION_MINOR = $(shell echo $(VERSION) | cut -d. -f2)
VERSION_PATCH = $(shell echo $(VERSION) | cut -d. -f3)

# Build configuration
CC = gcc
AR = ar
RANLIB = ranlib
INSTALL = install
MKDIR_P = mkdir -p
RM = rm -f
RMDIR = rm -rf

# Installation directories
PREFIX = /usr/local
EXEC_PREFIX = $(PREFIX)
BINDIR = $(EXEC_PREFIX)/bin
LIBDIR = $(EXEC_PREFIX)/lib
INCLUDEDIR = $(PREFIX)/include
DATADIR = $(PREFIX)/share
MANDIR = $(DATADIR)/man
PKGCONFIGDIR = $(LIBDIR)/pkgconfig

# Build directories
SRCDIR = src
INCDIR = include
BUILDDIR = build
OBJDIR = $(BUILDDIR)/obj
BINDIR_LOCAL = $(BUILDDIR)/bin
LIBDIR_LOCAL = $(BUILDDIR)/lib
TESTDIR = tests
EXAMPLEDIR = examples
TOOLSDIR = tools
MANDIR_LOCAL = man

# Compiler and linker flags
CSTD = -std=c99
CFLAGS = $(CSTD) -pedantic -Wall -Wextra -Wshadow -Wpointer-arith \
         -Wcast-align -Wwrite-strings -Wmissing-prototypes \
         -Wmissing-declarations -Wredundant-decls -Wnested-externs \
         -Winline -Wno-long-long -Wuninitialized -Wconversion \
         -Wstrict-prototypes -Wbad-function-cast \
         -I$(INCDIR) -I$(BUILDDIR)

LDFLAGS = -L$(LIBDIR_LOCAL)
LIBS = -lm

# Feature detection and configuration
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

# Platform-specific settings
ifeq ($(UNAME_S),Linux)
    PLATFORM = LINUX
    CFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
endif
ifeq ($(UNAME_S),Darwin)
    PLATFORM = MACOS
    CFLAGS += -D_DARWIN_C_SOURCE
endif
ifeq ($(findstring CYGWIN,$(UNAME_S)),CYGWIN)
    PLATFORM = WINDOWS
    CFLAGS += -D_WIN32
endif
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    PLATFORM = WINDOWS
    CFLAGS += -D_WIN32
endif

# Build type configuration
BUILD_TYPE ?= release
ifeq ($(BUILD_TYPE),debug)
    CFLAGS += -g -O0 -DDEBUG=1 -DREASONS_DEBUG=1 -fno-omit-frame-pointer
    ifneq ($(DISABLE_ASAN),1)
        CFLAGS += -fsanitize=address,undefined
        LDFLAGS += -fsanitize=address,undefined
    endif
else
    CFLAGS += -O2 -DNDEBUG
endif

# Coverage support
ifeq ($(ENABLE_COVERAGE),1)
    CFLAGS += --coverage
    LDFLAGS += --coverage
endif

# Optional readline support
HAS_READLINE := $(shell pkg-config --exists readline && echo 1 || echo 0)
ifeq ($(HAS_READLINE),1)
    CFLAGS += -DHAVE_READLINE=1 $(shell pkg-config --cflags readline)
    LIBS += $(shell pkg-config --libs readline)
endif

# Version information
CFLAGS += -DPACKAGE_NAME=\"$(PACKAGE_NAME)\" \
          -DPACKAGE_VERSION=\"$(VERSION)\" \
          -DVERSION_MAJOR=$(VERSION_MAJOR) \
          -DVERSION_MINOR=$(VERSION_MINOR) \
          -DVERSION_PATCH=$(VERSION_PATCH) \
          -D$(PLATFORM)=1

# Source files
CORE_SOURCES = $(wildcard $(SRCDIR)/core/*.c)
DEBUG_SOURCES = $(wildcard $(SRCDIR)/debug/*.c)
REPL_SOURCES = $(wildcard $(SRCDIR)/repl/*.c)
VIZ_SOURCES = $(wildcard $(SRCDIR)/viz/*.c)
IO_SOURCES = $(wildcard $(SRCDIR)/io/*.c)
STDLIB_SOURCES = $(wildcard $(SRCDIR)/stdlib/*.c)
UTILS_SOURCES = $(wildcard $(SRCDIR)/utils/*.c)

LIB_SOURCES = $(CORE_SOURCES) $(DEBUG_SOURCES) $(REPL_SOURCES) \
              $(VIZ_SOURCES) $(IO_SOURCES) $(STDLIB_SOURCES) \
              $(UTILS_SOURCES)

CLI_SOURCES = $(wildcard $(SRCDIR)/cli/*.c)

# Object files
LIB_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(LIB_SOURCES))
CLI_OBJECTS = $(patsubst $(SRCDIR)/cli/%.c,$(OBJDIR)/cli/%.o,$(CLI_SOURCES))

# Test sources and objects
TEST_SOURCES = $(wildcard $(TESTDIR)/*.c) \
               $(wildcard $(TESTDIR)/unit/*.c) \
               $(wildcard $(TESTDIR)/integration/*.c)
TEST_OBJECTS = $(patsubst $(TESTDIR)/%.c,$(OBJDIR)/tests/%.o,$(TEST_SOURCES))

# Targets
LIBRARY = $(LIBDIR_LOCAL)/lib$(PACKAGE_NAME).a
EXECUTABLES = $(BINDIR_LOCAL)/reasons \
              $(BINDIR_LOCAL)/reasons-compile \
              $(BINDIR_LOCAL)/reasons-run \
              $(BINDIR_LOCAL)/reasons-debug \
              $(BINDIR_LOCAL)/reasons-test-cli
TEST_EXECUTABLE = $(BINDIR_LOCAL)/reasons-test
BENCHMARK_EXECUTABLE = $(BINDIR_LOCAL)/reasons-benchmark

# Build configuration file
CONFIG_H = $(BUILDDIR)/config.h

# Default target
.PHONY: all
all: $(CONFIG_H) $(LIBRARY) $(EXECUTABLES)

# Create build directories
$(OBJDIR) $(BINDIR_LOCAL) $(LIBDIR_LOCAL) $(BUILDDIR):
	$(MKDIR_P) $@

$(OBJDIR)/core $(OBJDIR)/debug $(OBJDIR)/repl $(OBJDIR)/viz \
$(OBJDIR)/io $(OBJDIR)/stdlib $(OBJDIR)/utils $(OBJDIR)/cli \
$(OBJDIR)/tests $(OBJDIR)/tests/unit $(OBJDIR)/tests/integration: | $(OBJDIR)
	$(MKDIR_P) $@

# Generate config.h
$(CONFIG_H): | $(BUILDDIR)
	@echo "Generating $@"
	@echo "/* Generated configuration header */" > $@
	@echo "#ifndef CONFIG_H" >> $@
	@echo "#define CONFIG_H" >> $@
	@echo "" >> $@
	@echo "#define PACKAGE_NAME \"$(PACKAGE_NAME)\"" >> $@
	@echo "#define PACKAGE_VERSION \"$(VERSION)\"" >> $@
	@echo "#define VERSION_MAJOR $(VERSION_MAJOR)" >> $@
	@echo "#define VERSION_MINOR $(VERSION_MINOR)" >> $@
	@echo "#define VERSION_PATCH $(VERSION_PATCH)" >> $@
	@echo "#define $(PLATFORM) 1" >> $@
ifeq ($(HAS_READLINE),1)
	@echo "#define HAVE_READLINE 1" >> $@
endif
ifeq ($(BUILD_TYPE),debug)
	@echo "#define DEBUG 1" >> $@
	@echo "#define REASONS_DEBUG 1" >> $@
endif
	@echo "" >> $@
	@echo "#endif /* CONFIG_H */" >> $@

# Library
$(LIBRARY): $(LIB_OBJECTS) | $(LIBDIR_LOCAL)
	@echo "Creating library $@"
	$(AR) rcs $@ $^
	$(RANLIB) $@

# Object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)/core $(OBJDIR)/debug $(OBJDIR)/repl $(OBJDIR)/viz $(OBJDIR)/io $(OBJDIR)/stdlib $(OBJDIR)/utils $(OBJDIR)/cli
	@echo "Compiling $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Executables
$(BINDIR_LOCAL)/reasons: $(OBJDIR)/cli/main.o $(LIBRARY) | $(BINDIR_LOCAL)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) $< -l$(PACKAGE_NAME) $(LIBS) -o $@

$(BINDIR_LOCAL)/reasons-compile: $(OBJDIR)/cli/compile.o $(LIBRARY) | $(BINDIR_LOCAL)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) $< -l$(PACKAGE_NAME) $(LIBS) -o $@

$(BINDIR_LOCAL)/reasons-run: $(OBJDIR)/cli/run.o $(LIBRARY) | $(BINDIR_LOCAL)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) $< -l$(PACKAGE_NAME) $(LIBS) -o $@

$(BINDIR_LOCAL)/reasons-debug: $(OBJDIR)/cli/debug.o $(LIBRARY) | $(BINDIR_LOCAL)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) $< -l$(PACKAGE_NAME) $(LIBS) -o $@

$(BINDIR_LOCAL)/reasons-test-cli: $(OBJDIR)/cli/test.o $(LIBRARY) | $(BINDIR_LOCAL)
	@echo "Linking $@"
	$(CC) $(LDFLAGS) $< -l$(PACKAGE_NAME) $(LIBS) -o $@

# Tests
.PHONY: tests
tests: $(TEST_EXECUTABLE)

$(OBJDIR)/tests/%.o: $(TESTDIR)/%.c | $(OBJDIR)/tests $(OBJDIR)/tests/unit $(OBJDIR)/tests/integration
	@echo "Compiling test $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_EXECUTABLE): $(TEST_OBJECTS) $(LIBRARY) | $(BINDIR_LOCAL)
	@echo "Linking test executable $@"
	$(CC) $(LDFLAGS) $(TEST_OBJECTS) -l$(PACKAGE_NAME) $(LIBS) -o $@

# Benchmarks
.PHONY: benchmarks
benchmarks: $(BENCHMARK_EXECUTABLE)

$(BENCHMARK_EXECUTABLE): $(OBJDIR)/tests/integration/test_performance.o $(LIBRARY) | $(BINDIR_LOCAL)
	@echo "Linking benchmark executable $@"
	$(CC) $(LDFLAGS) $< -l$(PACKAGE_NAME) $(LIBS) -o $@

# Run targets
.PHONY: check test
check test: $(TEST_EXECUTABLE)
	@echo "Running tests..."
	$(TEST_EXECUTABLE)

.PHONY: check-unit
check-unit: $(TEST_EXECUTABLE)
	@echo "Running unit tests..."
	$(TEST_EXECUTABLE) --unit

.PHONY: check-integration
check-integration: $(TEST_EXECUTABLE)
	@echo "Running integration tests..."
	$(TEST_EXECUTABLE) --integration

.PHONY: benchmark
benchmark: $(BENCHMARK_EXECUTABLE)
	@echo "Running benchmarks..."
	$(BENCHMARK_EXECUTABLE)

# Development tools
.PHONY: format
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Formatting source code..."; \
		find $(SRCDIR) $(INCDIR) -name "*.c" -o -name "*.h" | xargs clang-format -i; \
	else \
		echo "clang-format not found, skipping formatting"; \
	fi

.PHONY: lint
lint:
	@if command -v clang-tidy >/dev/null 2>&1; then \
		echo "Running static analysis..."; \
		find $(SRCDIR) -name "*.c" | xargs clang-tidy; \
	else \
		echo "clang-tidy not found, skipping static analysis"; \
	fi

.PHONY: coverage
coverage:
ifeq ($(ENABLE_COVERAGE),1)
	@if command -v lcov >/dev/null 2>&1 && command -v genhtml >/dev/null 2>&1; then \
		echo "Generating coverage report..."; \
		$(MKDIR_P) $(BUILDDIR)/coverage; \
		lcov --directory . --capture --output-file $(BUILDDIR)/coverage/coverage.info; \
		lcov --remove $(BUILDDIR)/coverage/coverage.info '/usr/*' --output-file $(BUILDDIR)/coverage/coverage.info; \
		genhtml -o $(BUILDDIR)/coverage $(BUILDDIR)/coverage/coverage.info; \
		echo "Coverage report generated in $(BUILDDIR)/coverage/"; \
	else \
		echo "lcov/genhtml not found, cannot generate coverage report"; \
	fi
else
	@echo "Coverage not enabled. Use ENABLE_COVERAGE=1 make coverage"
endif

# pkg-config file
$(BUILDDIR)/$(PACKAGE_NAME).pc: | $(BUILDDIR)
	@echo "Generating pkg-config file $@"
	@echo "prefix=$(PREFIX)" > $@
	@echo "exec_prefix=$(EXEC_PREFIX)" >> $@
	@echo "libdir=$(LIBDIR)" >> $@
	@echo "includedir=$(INCLUDEDIR)" >> $@
	@echo "" >> $@
	@echo "Name: $(PACKAGE_NAME)" >> $@
	@echo "Description: $(DESCRIPTION)" >> $@
	@echo "Version: $(VERSION)" >> $@
	@echo "URL: $(HOMEPAGE)" >> $@
	@echo "Cflags: -I\$${includedir}/$(PACKAGE_NAME)" >> $@
	@echo "Libs: -L\$${libdir} -l$(PACKAGE_NAME)" >> $@
	@echo "Libs.private: $(LIBS)" >> $@

# Installation
.PHONY: install
install: all $(BUILDDIR)/$(PACKAGE_NAME).pc
	@echo "Installing $(PACKAGE_NAME)..."
	$(MKDIR_P) $(DESTDIR)$(BINDIR)
	$(MKDIR_P) $(DESTDIR)$(LIBDIR)
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/$(PACKAGE_NAME)
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/$(PACKAGE_NAME)/$(PACKAGE_NAME)
	$(MKDIR_P) $(DESTDIR)$(INCLUDEDIR)/$(PACKAGE_NAME)/utils
	$(MKDIR_P) $(DESTDIR)$(DATADIR)/$(PACKAGE_NAME)
	$(MKDIR_P) $(DESTDIR)$(MANDIR)/man1
	$(MKDIR_P) $(DESTDIR)$(PKGCONFIGDIR)
	
	# Install executables
	$(INSTALL) -m 755 $(EXECUTABLES) $(DESTDIR)$(BINDIR)
	
	# Install library
	$(INSTALL) -m 644 $(LIBRARY) $(DESTDIR)$(LIBDIR)
	
	# Install headers
	$(INSTALL) -m 644 $(INCDIR)/reasons.h $(DESTDIR)$(INCLUDEDIR)/$(PACKAGE_NAME)/
	$(INSTALL) -m 644 $(INCDIR)/reasons/*.h $(DESTDIR)$(INCLUDEDIR)/$(PACKAGE_NAME)/$(PACKAGE_NAME)/
	$(INSTALL) -m 644 $(INCDIR)/utils/*.h $(DESTDIR)$(INCLUDEDIR)/$(PACKAGE_NAME)/utils/
	
	# Install examples
	cp -r $(EXAMPLEDIR) $(DESTDIR)$(DATADIR)/$(PACKAGE_NAME)/
	
	# Install man pages
	$(INSTALL) -m 644 $(MANDIR_LOCAL)/*.1 $(DESTDIR)$(MANDIR)/man1/
	
	# Install pkg-config file
	$(INSTALL) -m 644 $(BUILDDIR)/$(PACKAGE_NAME).pc $(DESTDIR)$(PKGCONFIGDIR)/

.PHONY: uninstall
uninstall:
	@echo "Uninstalling $(PACKAGE_NAME)..."
	$(RM) $(DESTDIR)$(BINDIR)/reasons*
	$(RM) $(DESTDIR)$(LIBDIR)/lib$(PACKAGE_NAME).a
	$(RMDIR) $(DESTDIR)$(INCLUDEDIR)/$(PACKAGE_NAME)
	$(RMDIR) $(DESTDIR)$(DATADIR)/$(PACKAGE_NAME)
	$(RM) $(DESTDIR)$(MANDIR)/man1/reasons*.1
	$(RM) $(DESTDIR)$(PKGCONFIGDIR)/$(PACKAGE_NAME).pc

# Clean targets
.PHONY: clean
clean:
	@echo "Cleaning build files..."
	$(RMDIR) $(BUILDDIR)

.PHONY: distclean
distclean: clean
	@echo "Cleaning distribution files..."
	$(RMDIR) dist

# Help
.PHONY: help
help:
	@echo "$(PACKAGE_NAME) $(VERSION) - $(DESCRIPTION)"
	@echo ""
	@echo "Available targets:"
	@echo "  all              Build library and executables (default)"
	@echo "  tests            Build test suite"
	@echo "  benchmarks       Build benchmarks"
	@echo "  check, test      Run all tests"
	@echo "  check-unit       Run unit tests only"
	@echo "  check-integration Run integration tests only"
	@echo "  benchmark        Run benchmarks"
	@echo "  install          Install to system"
	@echo "  uninstall        Remove from system"
	@echo "  format           Format source code (requires clang-format)"
	@echo "  lint             Run static analysis (requires clang-tidy)"
	@echo "  coverage         Generate coverage report (requires ENABLE_COVERAGE=1)"
	@echo "  clean            Clean build files"
	@echo "  distclean        Clean build and distribution files"
	@echo "  help             Show this help message"
	@echo ""
	@echo "Build options:"
	@echo "  BUILD_TYPE=debug|release    Build type (default: release)"
	@echo "  ENABLE_COVERAGE=1           Enable code coverage"
	@echo "  DISABLE_ASAN=1              Disable AddressSanitizer in debug builds"
	@echo "  PREFIX=/path                Installation prefix (default: /usr/local)"
	@echo ""
	@echo "Examples:"
	@echo "  make BUILD_TYPE=debug       Debug build"
	@echo "  make ENABLE_COVERAGE=1 test Coverage-enabled test run"
	@echo "  make PREFIX=/opt/reasons install Install to /opt/reasons"

# Dependencies
-include $(LIB_OBJECTS:.o=.d)
-include $(CLI_OBJECTS:.o=.d)
-include $(TEST_OBJECTS:.o=.d)

# Generate dependency files
$(OBJDIR)/%.d: $(SRCDIR)/%.c
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@

$(OBJDIR)/tests/%.d: $(TESTDIR)/%.c
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@

.PRECIOUS: $(OBJDIR)/%.d $(OBJDIR)/tests/%.d
