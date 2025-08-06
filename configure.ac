# configure.ac - GNU Autotools configuration for Reasons Decision Tree Debugger
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([reasons], [0.1.0], [https://github.com/aitzazisstuffed/reasons/issues])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([src/core/parser.c])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AM_SILENT_RULES([yes])

# Program information
PACKAGE_DESCRIPTION="Decision Tree Debugger and DSL"
PACKAGE_URL="https://github.com/yourusername/reasons"
AC_SUBST([PACKAGE_DESCRIPTION])
AC_DEFINE_UNQUOTED([PACKAGE_DESCRIPTION], ["$PACKAGE_DESCRIPTION"], [Package description])
AC_DEFINE_UNQUOTED([PACKAGE_URL], ["$PACKAGE_URL"], [Package URL])

# Version information
VERSION_MAJOR=`echo $VERSION | cut -d. -f1`
VERSION_MINOR=`echo $VERSION | cut -d. -f2`  
VERSION_PATCH=`echo $VERSION | cut -d. -f3`
AC_SUBST([VERSION_MAJOR])
AC_SUBST([VERSION_MINOR])
AC_SUBST([VERSION_PATCH])
AC_DEFINE_UNQUOTED([VERSION_MAJOR], [$VERSION_MAJOR], [Major version])
AC_DEFINE_UNQUOTED([VERSION_MINOR], [$VERSION_MINOR], [Minor version])
AC_DEFINE_UNQUOTED([VERSION_PATCH], [$VERSION_PATCH], [Patch version])

# Checks for programs
AC_PROG_CC
AC_PROG_CC_C99
if test "$ac_cv_prog_cc_c99" = no; then
    AC_MSG_ERROR([C99 compiler required])
fi

AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AM_PROG_AR

# Check for pkg-config
PKG_PROG_PKG_CONFIG

# Compiler flags
AC_SUBST([AM_CFLAGS], ['-std=c99 -pedantic -Wall -Wextra -Wshadow -Wpointer-arith -Wcast-align -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wnested-externs -Winline -Wno-long-long -Wuninitialized -Wconversion -Wstrict-prototypes -Wbad-function-cast'])

# Debug build support
AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug], [Enable debug build (default: no)])],
    [debug=$enableval],
    [debug=no])

if test "x$debug" = "xyes"; then
    AC_DEFINE([DEBUG], [1], [Enable debug build])
    AC_DEFINE([REASONS_DEBUG], [1], [Enable reasons debug features])
    AC_SUBST([AM_CFLAGS], ["$AM_CFLAGS -g -O0 -DDEBUG=1 -DREASONS_DEBUG=1"])
    
    # AddressSanitizer support
    AC_ARG_ENABLE([asan],
        [AS_HELP_STRING([--enable-asan], [Enable AddressSanitizer (default: no)])],
        [asan=$enableval],
        [asan=no])
    
    if test "x$asan" = "xyes"; then
        AC_SUBST([AM_CFLAGS], ["$AM_CFLAGS -fsanitize=address,undefined -fno-omit-frame-pointer"])
        AC_SUBST([AM_LDFLAGS], ["$AM_LDFLAGS -fsanitize=address,undefined"])
    fi
else
    AC_SUBST([AM_CFLAGS], ["$AM_CFLAGS -O2 -DNDEBUG"])
fi

# Feature toggles
AC_ARG_ENABLE([tests],
    [AS_HELP_STRING([--enable-tests], [Build test suite (default: yes)])],
    [tests=$enableval],
    [tests=yes])
AM_CONDITIONAL([ENABLE_TESTS], [test "x$tests" = "xyes"])

AC_ARG_ENABLE([benchmarks],
    [AS_HELP_STRING([--enable-benchmarks], [Build benchmarks (default: no)])],
    [benchmarks=$enableval],
    [benchmarks=no])
AM_CONDITIONAL([ENABLE_BENCHMARKS], [test "x$benchmarks" = "xyes"])

AC_ARG_ENABLE([docs],
    [AS_HELP_STRING([--enable-docs], [Build documentation (default: yes)])],
    [docs=$enableval],
    [docs=yes])
AM_CONDITIONAL([ENABLE_DOCS], [test "x$docs" = "xyes"])

# Checks for libraries
AC_SEARCH_LIBS([sqrt], [m], [], [AC_MSG_ERROR([Math library required])])

# Optional readline support
AC_ARG_WITH([readline],
    [AS_HELP_STRING([--with-readline], [Use GNU readline for REPL (default: auto)])],
    [with_readline=$withval],
    [with_readline=auto])

have_readline=no
if test "x$with_readline" != "xno"; then
    AC_CHECK_LIB([readline], [readline],
        [AC_CHECK_HEADERS([readline/readline.h readline/history.h],
            [have_readline=yes
             READLINE_LIBS="-lreadline"
             AC_DEFINE([HAVE_READLINE], [1], [Have GNU readline])],
            [if test "x$with_readline" = "xyes"; then
                AC_MSG_ERROR([readline headers not found])
             fi])],
        [if test "x$with_readline" = "xyes"; then
            AC_MSG_ERROR([readline library not found])
         fi])
fi
AC_SUBST([READLINE_LIBS])
AM_CONDITIONAL([HAVE_READLINE], [test "x$have_readline" = "xyes"])

# Checks for header files
AC_CHECK_HEADERS([assert.h ctype.h errno.h limits.h signal.h stdarg.h stdbool.h stddef.h stdint.h stdio.h stdlib.h string.h time.h], 
    [], [AC_MSG_ERROR([Required standard header not found])])

# POSIX headers
AC_CHECK_HEADERS([unistd.h sys/stat.h sys/types.h fcntl.h])

# Checks for typedefs, structures, and compiler characteristics
AC_CHECK_HEADER_STDBOOL
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_INT8_T
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T

# Checks for library functions
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([strlen strcpy strcmp strncmp sprintf snprintf], 
    [], [AC_MSG_ERROR([Required string function not found])])

# Optional functions
AC_CHECK_FUNCS([strnlen strdup strndup getline asprintf vasprintf])

# Platform-specific checks
AC_CANONICAL_HOST
case $host_os in
    mingw* | cygwin* | msys*)
        AM_CONDITIONAL([WINDOWS], [true])
        AC_DEFINE([WINDOWS], [1], [Building on Windows])
        ;;
    darwin*)
        AM_CONDITIONAL([MACOS], [true])
        AC_DEFINE([MACOS], [1], [Building on macOS])
        ;;
    linux*)
        AM_CONDITIONAL([LINUX], [true])
        AC_DEFINE([LINUX], [1], [Building on Linux])
        ;;
    *)
        AM_CONDITIONAL([UNIX], [true])
        AC_DEFINE([UNIX], [1], [Building on Unix-like system])
        ;;
esac

AM_CONDITIONAL([WINDOWS], [false])
AM_CONDITIONAL([MACOS], [false])
AM_CONDITIONAL([LINUX], [false])
AM_CONDITIONAL([UNIX], [false])

# Large file support
AC_SYS_LARGEFILE

# Thread support (if needed in future)
AX_PTHREAD

# Check for development tools
AC_CHECK_PROGS([CLANG_FORMAT], [clang-format])
AC_CHECK_PROGS([CLANG_TIDY], [clang-tidy])
AC_CHECK_PROGS([VALGRIND], [valgrind])
AC_CHECK_PROGS([GDB], [gdb])

# Documentation tools
if test "x$docs" = "xyes"; then
    AC_CHECK_PROGS([DOXYGEN], [doxygen])
    AC_CHECK_PROGS([SPHINX_BUILD], [sphinx-build])
    
    if test -z "$DOXYGEN" && test -z "$SPHINX_BUILD"; then
        AC_MSG_WARN([Neither doxygen nor sphinx-build found, documentation will not be built])
    fi
fi

# Coverage support
AC_ARG_ENABLE([coverage],
    [AS_HELP_STRING([--enable-coverage], [Enable coverage analysis (default: no)])],
    [coverage=$enableval],
    [coverage=no])

if test "x$coverage" = "xyes"; then
    AC_CHECK_PROGS([GCOV], [gcov])
    AC_CHECK_PROGS([LCOV], [lcov])
    AC_CHECK_PROGS([GENHTML], [genhtml])
    
    if test -z "$GCOV" || test -z "$LCOV" || test -z "$GENHTML"; then
        AC_MSG_ERROR([Coverage tools (gcov, lcov, genhtml) required for --enable-coverage])
    fi
    
    AC_SUBST([AM_CFLAGS], ["$AM_CFLAGS --coverage"])
    AC_SUBST([AM_LDFLAGS], ["$AM_LDFLAGS --coverage"])
    AC_DEFINE([COVERAGE], [1], [Enable coverage analysis])
fi
AM_CONDITIONAL([ENABLE_COVERAGE], [test "x$coverage" = "xyes"])

# Output files
AC_CONFIG_FILES([
    Makefile
    src/Makefile
    src/core/Makefile
    src/debug/Makefile
    src/repl/Makefile
    src/viz/Makefile
    src/io/Makefile
    src/stdlib/Makefile
    src/cli/Makefile
    src/utils/Makefile
    tests/Makefile
    tests/unit/Makefile
    tests/integration/Makefile
    examples/Makefile
    man/Makefile
    docs/Makefile
    tools/Makefile
    scripts/Makefile
    reasons.pc
])

# Generate the configure script
AC_OUTPUT

# Configuration summary
echo
echo "========================================"
echo "  $PACKAGE_NAME $VERSION Configuration"
echo "========================================"
echo "  Host system type:      $host"
echo "  C compiler:            $CC"
echo "  C compiler flags:      $AM_CFLAGS"
echo "  Linker flags:          $AM_LDFLAGS"
echo "  Install prefix:        $prefix"
echo
echo "  Features:"
echo "    Debug build:         $debug"
echo "    Tests:               $tests"
echo "    Benchmarks:          $benchmarks"
echo "    Documentation:       $docs"
echo "    Coverage analysis:   $coverage"
echo "    GNU Readline:        $have_readline"
if test "x$debug" = "xyes"; then
echo "    AddressSanitizer:    $asan"
fi
echo
echo "  Development tools:"
echo "    clang-format:        ${CLANG_FORMAT:-no}"
echo "    clang-tidy:          ${CLANG_TIDY:-no}"
echo "    valgrind:            ${VALGRIND:-no}"
echo "    gdb:                 ${GDB:-no}"
if test "x$docs" = "xyes"; then
echo "    doxygen:             ${DOXYGEN:-no}"
echo "    sphinx-build:        ${SPHINX_BUILD:-no}"
fi
if test "x$coverage" = "xyes"; then
echo "    gcov:                ${GCOV:-no}"
echo "    lcov:                ${LCOV:-no}"
echo "    genhtml:             ${GENHTML:-no}"
fi
echo
echo "Run 'make' to build $PACKAGE_NAME"
if test "x$tests" = "xyes"; then
echo "Run 'make check' to run the test suite"
fi
echo "Run 'make install' to install $PACKAGE_NAME"
echo
