# CMakeLists.txt - CMake build configuration for Reasons Decision Tree Debugger
cmake_minimum_required(VERSION 3.12)

# Project definition
project(reasons
    VERSION 0.1.0
    DESCRIPTION "Decision Tree Debugger and DSL"
    HOMEPAGE_URL "https://github.com/aitzazisstuffed/reasons"
    LANGUAGES C
)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build options
option(REASONS_BUILD_TESTS "Build test suite" ON)
option(REASONS_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(REASONS_BUILD_DOCS "Build documentation" ON)
option(REASONS_WITH_READLINE "Use GNU Readline for REPL" ON)
option(REASONS_ENABLE_COVERAGE "Enable code coverage" OFF)
option(REASONS_ENABLE_ASAN "Enable AddressSanitizer (debug builds)" OFF)
option(REASONS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (debug builds)" OFF)

# Version components
set(REASONS_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(REASONS_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(REASONS_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# Include modules
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Platform detection
if(WIN32)
    set(REASONS_PLATFORM_WINDOWS 1)
elseif(APPLE)
    set(REASONS_PLATFORM_MACOS 1)
elseif(UNIX)
    set(REASONS_PLATFORM_LINUX 1)
endif()

# Compiler flags
set(REASONS_C_FLAGS
    -Wall
    -Wextra
    -Wshadow
    -Wpointer-arith
    -Wcast-align
    -Wwrite-strings
    -Wmissing-prototypes
    -Wmissing-declarations
    -Wredundant-decls
    -Wnested-externs
    -Winline
    -Wno-long-long
    -Wuninitialized
    -Wconversion
    -Wstrict-prototypes
    -Wbad-function-cast
    -pedantic
)

# Debug flags
set(REASONS_C_FLAGS_DEBUG
    -g
    -O0
    -DDEBUG=1
    -DREASONS_DEBUG=1
    -fno-omit-frame-pointer
)

# Release flags
set(REASONS_C_FLAGS_RELEASE
    -O2
    -DNDEBUG
)

# Apply compiler flags
add_compile_options(${REASONS_C_FLAGS})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(${REASONS_C_FLAGS_DEBUG})
else()
    add_compile_options(${REASONS_C_FLAGS_RELEASE})
endif()

# Sanitizers (debug build only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(REASONS_ENABLE_ASAN)
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
    
    if(REASONS_ENABLE_UBSAN)
        add_compile_options(-fsanitize=undefined)
        add_link_options(-fsanitize=undefined)
    endif()
endif()

# Coverage support
if(REASONS_ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
    find_program(LCOV_PATH lcov REQUIRED)
    find_program(GENHTML_PATH genhtml REQUIRED)
endif()

# Check for required headers
set(REQUIRED_HEADERS
    assert.h
    ctype.h
    errno.h
    limits.h
    signal.h
    stdarg.h
    stdbool.h
    stddef.h
    stdint.h
    stdio.h
    stdlib.h
    string.h
    time.h
)

foreach(header ${REQUIRED_HEADERS})
    check_include_file(${header} HAVE_${header})
    if(NOT HAVE_${header})
        message(FATAL_ERROR "Required header ${header} not found")
    endif()
endforeach()

# Check for POSIX headers
set(POSIX_HEADERS unistd.h sys/stat.h sys/types.h fcntl.h)
foreach(header ${POSIX_HEADERS})
    string(REPLACE "/" "_" header_var ${header})
    string(REPLACE "." "_" header_var ${header_var})
    string(TOUPPER ${header_var} header_var)
    check_include_file(${header} HAVE_${header_var})
endforeach()

# Check for required functions
set(REQUIRED_FUNCTIONS
    malloc calloc realloc free
    strlen strcpy strcmp strncmp
    sprintf snprintf
)

foreach(func ${REQUIRED_FUNCTIONS})
    check_function_exists(${func} HAVE_${func})
    if(NOT HAVE_${func})
        message(FATAL_ERROR "Required function ${func} not found")
    endif()
endforeach()

# Check for optional functions
set(OPTIONAL_FUNCTIONS strnlen strdup strndup getline asprintf vasprintf)
foreach(func ${OPTIONAL_FUNCTIONS})
    string(TOUPPER ${func} func_upper)
    check_function_exists(${func} HAVE_${func_upper})
endforeach()

# Find dependencies
find_library(MATH_LIBRARY m)
if(NOT MATH_LIBRARY)
    message(FATAL_ERROR "Math library not found")
endif()

# Optional readline support
set(HAVE_READLINE 0)
if(REASONS_WITH_READLINE)
    find_path(READLINE_INCLUDE_DIR readline/readline.h)
    find_library(READLINE_LIBRARY readline)
    
    if(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
        set(HAVE_READLINE 1)
        message(STATUS "Found GNU Readline: ${READLINE_LIBRARY}")
    else()
        message(STATUS "GNU Readline not found, REPL will have basic functionality")
    endif()
endif()

# Configure config.h
configure_file(
    "${CMAKE_SOURCE_DIR}/config.h.in"
    "${CMAKE_BINARY_DIR}/config.h"
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

# Source files
set(CORE_SOURCES
    src/core/ast.c
    src/core/lexer.c
    src/core/parser.c
    src/core/eval.c
    src/core/trace.c
    src/core/explain.c
    src/core/tree.c
    src/core/runtime.c
    src/core/memory.c
)

set(DEBUG_SOURCES
    src/debug/debugger.c
    src/debug/breakpoints.c
    src/debug/watch.c
    src/debug/history.c
    src/debug/coverage.c
    src/debug/profiler.c
)

set(REPL_SOURCES
    src/repl/repl.c
    src/repl/commands.c
    src/repl/completion.c
    src/repl/history.c
    src/repl/prompt.c
)

set(VIZ_SOURCES
    src/viz/tree_viz.c
    src/viz/trace_viz.c
    src/viz/graph.c
    src/viz/ascii_art.c
)

set(IO_SOURCES
    src/io/fileio.c
    src/io/json_io.c
    src/io/csv_io.c
    src/io/config.c
)

set(STDLIB_SOURCES
    src/stdlib/math.c
    src/stdlib/string.c
    src/stdlib/datetime.c
    src/stdlib/stats.c
    src/stdlib/validation.c
)

set(UTILS_SOURCES
    src/utils/error.c
    src/utils/logger.c
    src/utils/string_utils.c
    src/utils/hash.c
    src/utils/vector.c
)

# Core library
add_library(reasons STATIC
    ${CORE_SOURCES}
    ${DEBUG_SOURCES}
    ${REPL_SOURCES}
    ${VIZ_SOURCES}
    ${IO_SOURCES}
    ${STDLIB_SOURCES}
    ${UTILS_SOURCES}
)

target_link_libraries(reasons ${MATH_LIBRARY})
if(HAVE_READLINE)
    target_link_libraries(reasons ${READLINE_LIBRARY})
endif()

# Executables
add_executable(reasons-main src/cli/main.c)
target_link_libraries(reasons-main reasons)
set_target_properties(reasons-main PROPERTIES OUTPUT_NAME reasons)

add_executable(reasons-compile src/cli/compile.c)
target_link_libraries(reasons-compile reasons)

add_executable(reasons-run src/cli/run.c)
target_link_libraries(reasons-run reasons)

add_executable(reasons-debug src/cli/debug.c)
target_link_libraries(reasons-debug reasons)

add_executable(reasons-test-cli src/cli/test.c)
target_link_libraries(reasons-test-cli reasons)

# Tests
if(REASONS_BUILD_TESTS)
    enable_testing()
    
    set(TEST_SOURCES
        tests/test_runner.c
        tests/test_helpers.c
        tests/unit/test_lexer.c
        tests/unit/test_parser.c
        tests/unit/test_eval.c
        tests/unit/test_trace.c
        tests/unit/test_debugger.c
        tests/unit/test_memory.c
        tests/unit/test_utils.c
        tests/integration/test_repl.c
        tests/integration/test_cli.c
        tests/integration/test_examples.c
    )
    
    add_executable(reasons-test ${TEST_SOURCES})
    target_link_libraries(reasons-test reasons)
    
    add_test(NAME unit_tests COMMAND reasons-test --unit)
    add_test(NAME integration_tests COMMAND reasons-test --integration)
    add_test(NAME all_tests COMMAND reasons-test)
endif()

# Benchmarks
if(REASONS_BUILD_BENCHMARKS)
    add_executable(reasons-benchmark tests/integration/test_performance.c)
    target_link_libraries(reasons-benchmark reasons)
endif()

# Installation
install(TARGETS reasons-main reasons-compile reasons-run reasons-debug reasons-test-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS reasons
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES include/reasons.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/reasons
)

install(DIRECTORY include/reasons/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/reasons/reasons
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY include/utils/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/reasons/utils
    FILES_MATCHING PATTERN "*.h"
)

install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/reasons/examples
    FILES_MATCHING PATTERN "*.reasons"
)

install(FILES man/reasons.1 man/reasons-debug.1 man/reasons-run.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

# pkg-config file
configure_file(
    "${CMAKE_SOURCE_DIR}/reasons.pc.in"
    "${CMAKE_BINARY_DIR}/reasons.pc"
    @ONLY
)

install(FILES "${CMAKE_BINARY_DIR}/reasons.pc"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

# Custom targets
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i src/**/*.c include/**/*.h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Formatting source code"
    )
endif()

find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    add_custom_target(lint
        COMMAND ${CLANG_TIDY} src/**/*.c include/**/*.h
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running static analysis"
    )
endif()

if(REASONS_ENABLE_COVERAGE)
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory coverage
        COMMAND ${LCOV_PATH} --directory . --capture --output-file coverage/coverage.info
        COMMAND ${LCOV_PATH} --remove coverage/coverage.info '/usr/*' --output-file coverage/coverage.info
        COMMAND ${GENHTML_PATH} -o coverage coverage/coverage.info
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage report"
    )
endif()

# Summary
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  ${PROJECT_NAME} ${PROJECT_VERSION} Configuration")
message(STATUS "========================================")
message(STATUS "  Build type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "  C compiler:           ${CMAKE_C_COMPILER}")
message(STATUS "  Install prefix:       ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
message(STATUS "  Features:")
message(STATUS "    Tests:              ${REASONS_BUILD_TESTS}")
message(STATUS "    Benchmarks:         ${REASONS_BUILD_BENCHMARKS}")
message(STATUS "    Documentation:      ${REASONS_BUILD_DOCS}")
message(STATUS "    Coverage:           ${REASONS_ENABLE_COVERAGE}")
message(STATUS "    GNU Readline:       ${HAVE_READLINE}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
message(STATUS "    AddressSanitizer:   ${REASONS_ENABLE_ASAN}")
message(STATUS "    UBSanitizer:        ${REASONS_ENABLE_UBSAN}")
endif()
message(STATUS "")
message(STATUS "Run 'make' to build ${PROJECT_NAME}")
if(REASONS_BUILD_TESTS)
message(STATUS "Run 'make test' to run the test suite")
endif()
message(STATUS "Run 'make install' to install ${PROJECT_NAME}")
message(STATUS "")
