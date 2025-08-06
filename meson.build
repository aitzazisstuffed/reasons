# Meson build configuration for Reasons Decision Tree Debugger
# Modern build system with excellent cross-platform support

project('reasons', 'c',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'c_std=c99',
    'warning_level=3',
    'werror=true',
    'optimization=2',
    'debug=true',
    'strip=false',
    'buildtype=debugoptimized'
  ],
  meson_version: '>=0.56.0'
)

# Project information
project_name = meson.project_name()
project_version = meson.project_version()
project_license = meson.project_license()

# Configuration data
conf_data = configuration_data()
conf_data.set_quoted('PROJECT_NAME', project_name)
conf_data.set_quoted('PROJECT_VERSION', project_version)
conf_data.set_quoted('PROJECT_LICENSE', project_license[0])
conf_data.set('VERSION_MAJOR', project_version.split('.')[0].to_int())
conf_data.set('VERSION_MINOR', project_version.split('.')[1].to_int())
conf_data.set('VERSION_PATCH', project_version.split('.')[2].to_int())

# Check for required functions and headers
cc = meson.get_compiler('c')

# Standard C headers
required_headers = [
  'assert.h',
  'ctype.h',
  'errno.h', 
  'limits.h',
  'signal.h',
  'stdarg.h',
  'stdbool.h',
  'stddef.h',
  'stdint.h',
  'stdio.h',
  'stdlib.h',
  'string.h',
  'time.h'
]

foreach header : required_headers
  if not cc.has_header(header)
    error('Required header @0@ not found'.format(header))
  endif
endforeach

# POSIX headers (optional)
posix_headers = [
  'unistd.h',
  'sys/stat.h',
  'sys/types.h',
  'fcntl.h'
]

foreach header : posix_headers
  conf_data.set('HAVE_' + header.underscorify().to_upper(), 
                cc.has_header(header))
endforeach

# Check for functions
required_functions = [
  'malloc',
  'calloc',
  'realloc', 
  'free',
  'strlen',
  'strcpy',
  'strcmp',
  'strncmp',
  'sprintf',
  'snprintf'
]

foreach func : required_functions
  if not cc.has_function(func)
    error('Required function @0@ not found'.format(func))
  endif
endforeach

# Optional functions
optional_functions = [
  'strnlen',
  'strdup',
  'strndup',
  'getline',
  'asprintf',
  'vasprintf'
]

foreach func : optional_functions
  conf_data.set('HAVE_' + func.to_upper(), cc.has_function(func))
endforeach

# Check for specific features
conf_data.set('HAVE_GNU_READLINE', 
              cc.has_header('readline/readline.h') and 
              cc.has_header('readline/history.h'))

# Math library
math_dep = cc.find_library('m', required: false)

# Optional readline support for better REPL
readline_dep = dependency('readline', required: false)
if readline_dep.found()
  conf_data.set('HAVE_READLINE', 1)
endif

# Include directories
inc_dirs = include_directories('include')

# Compile flags
add_project_arguments([
  '-D_POSIX_C_SOURCE=200809L',
  '-D_DEFAULT_SOURCE',
  '-pedantic',
  '-Wextra',
  '-Wshadow',
  '-Wpointer-arith',
  '-Wcast-align',
  '-Wwrite-strings',
  '-Wmissing-prototypes',
  '-Wmissing-declarations',
  '-Wredundant-decls',
  '-Wnested-externs',
  '-Winline',
  '-Wno-long-long',
  '-Wuninitialized',
  '-Wconversion',
  '-Wstrict-prototypes',
  '-Wbad-function-cast'
], language: 'c')

# Debug build flags
if get_option('buildtype').startswith('debug')
  add_project_arguments([
    '-DDEBUG=1',
    '-DREASONS_DEBUG=1',
    '-fno-omit-frame-pointer',
    '-fsanitize=address,undefined'
  ], language: 'c')
  
  add_project_link_arguments([
    '-fsanitize=address,undefined'
  ], language: 'c')
endif

# Core library sources
core_sources = files(
  'src/core/ast.c',
  'src/core/lexer.c', 
  'src/core/parser.c',
  'src/core/eval.c',
  'src/core/trace.c',
  'src/core/explain.c',
  'src/core/tree.c',
  'src/core/runtime.c',
  'src/core/memory.c'
)

# Debug module sources
debug_sources = files(
  'src/debug/debugger.c',
  'src/debug/breakpoints.c',
  'src/debug/watch.c',
  'src/debug/history.c',
  'src/debug/coverage.c',
  'src/debug/profiler.c'
)

# REPL module sources
repl_sources = files(
  'src/repl/repl.c',
  'src/repl/commands.c',
  'src/repl/completion.c',
  'src/repl/history.c',
  'src/repl/prompt.c'
)

# Visualization sources
viz_sources = files(
  'src/viz/tree_viz.c',
  'src/viz/trace_viz.c',
  'src/viz/graph.c',
  'src/viz/ascii_art.c'
)

# I/O sources
io_sources = files(
  'src/io/fileio.c',
  'src/io/json_io.c',
  'src/io/csv_io.c',
  'src/io/config.c'
)

# Standard library sources
stdlib_sources = files(
  'src/stdlib/math.c',
  'src/stdlib/string.c',
  'src/stdlib/datetime.c',
  'src/stdlib/stats.c',
  'src/stdlib/validation.c'
)

# Utility sources
utils_sources = files(
  'src/utils/error.c',
  'src/utils/logger.c',
  'src/utils/string_utils.c',
  'src/utils/hash.c',
  'src/utils/vector.c'
)

# All library sources
lib_sources = [
  core_sources,
  debug_sources,
  repl_sources,
  viz_sources, 
  io_sources,
  stdlib_sources,
  utils_sources
]

# Generate config.h
config_h = configure_file(
  input: 'config.h.in',
  output: 'config.h',
  configuration: conf_data
)

# Core reasons library
reasons_lib = static_library('reasons',
  lib_sources,
  include_directories: inc_dirs,
  dependencies: [math_dep, readline_dep],
  install: false
)

# Main CLI executable
reasons_exe = executable('reasons',
  main_cli_source,
  include_directories: inc_dirs,
  link_with: reasons_lib,
  dependencies: [math_dep, readline_dep],
  install: true,
  install_dir: get_option('bindir')
)

# Compile mode executable  
reasons_compile_exe = executable('reasons-compile',
  compile_cli_source,
  include_directories: inc_dirs,
  link_with: reasons_lib,
  dependencies: [math_dep, readline_dep],
  install: true,
  install_dir: get_option('bindir')
)

# Run/REPL executable
reasons_run_exe = executable('reasons-run',
  run_cli_source,
  include_directories: inc_dirs,
  link_with: reasons_lib,
  dependencies: [math_dep, readline_dep],
  install: true,
  install_dir: get_option('bindir')
)

# Debug mode executable
reasons_debug_exe = executable('reasons-debug',
  debug_cli_source,
  include_directories: inc_dirs,
  link_with: reasons_lib,
  dependencies: [math_dep, readline_dep],
  install: true,
  install_dir: get_option('bindir')
)

# Test CLI executable
reasons_test_cli_exe = executable('reasons-test-cli',
  test_cli_source,
  include_directories: inc_dirs,
  link_with: reasons_lib,
  dependencies: [math_dep, readline_dep],
  install: true,
  install_dir: get_option('bindir')
)

# Test executable
if get_option('tests')
  test_sources = files(
    'tests/test_runner.c',
    'tests/test_helpers.c',
    'tests/unit/test_lexer.c',
    'tests/unit/test_parser.c',
    'tests/unit/test_eval.c',
    'tests/unit/test_trace.c',
    'tests/unit/test_debugger.c',
    'tests/unit/test_memory.c',
    'tests/unit/test_utils.c',
    'tests/integration/test_repl.c',
    'tests/integration/test_cli.c',
    'tests/integration/test_examples.c'
  )

  test_exe = executable('reasons-test',
    test_sources,
    include_directories: inc_dirs,
    link_with: reasons_lib,
    dependencies: [math_dep, readline_dep],
    install: false
  )

  # Register tests
  test('unit_tests', test_exe, args: ['--unit'])
  test('integration_tests', test_exe, args: ['--integration'])
  test('all_tests', test_exe)
endif

# Benchmarks
if get_option('benchmarks')
  benchmark_exe = executable('reasons-benchmark',
    'tests/integration/test_performance.c',
    include_directories: inc_dirs,
    link_with: reasons_lib,
    dependencies: [math_dep],
    install: false
  )
  
  benchmark('performance', benchmark_exe)
endif

# Install headers
install_headers(
  'include/reasons.h',
  subdir: 'reasons'
)

install_headers([
    'include/reasons/ast.h',
    'include/reasons/lexer.h',
    'include/reasons/parser.h',
    'include/reasons/eval.h',
    'include/reasons/trace.h',
    'include/reasons/explain.h',
    'include/reasons/debugger.h',
    'include/reasons/repl.h',
    'include/reasons/tree.h',
    'include/reasons/runtime.h',
    'include/reasons/viz.h',
    'include/reasons/types.h'
  ],
  subdir: 'reasons/reasons'
)

install_headers([
    'include/utils/error.h',
    'include/utils/logger.h',
    'include/utils/memory.h',
    'include/utils/collections.h'
  ],
  subdir: 'reasons/utils'
)

# Install man pages
install_man('man/reasons.1', 'man/reasons-debug.1', 'man/reasons-run.1')

# Install examples
install_subdir('examples', install_dir: get_option('datadir') / 'reasons')

# Install pkg-config file
pkg = import('pkgconfig')
pkg.generate(
  name: 'reasons',
  description: 'Decision Tree Debugger and DSL',
  version: project_version,
  libraries: reasons_lib,
  subdirs: 'reasons'
)

# Custom targets
run_target('format',
  command: ['tools/format.sh']
)

run_target('lint', 
  command: ['tools/lint.sh']
)

run_target('coverage',
  command: ['tools/coverage.sh']
)

# Summary
summary({
  'Version': project_version,
  'License': project_license[0],
  'C Compiler': cc.get_id(),
  'Build type': get_option('buildtype'),
  'Optimization': get_option('optimization'),
  'Debug info': get_option('debug'),
  'Tests': get_option('tests'),
  'Benchmarks': get_option('benchmarks'),
  'Readline support': readline_dep.found(),
  'Math library': math_dep.found(),
  'Install prefix': get_option('prefix'),
}, section: 'Configuration')

message('Reasons Decision Tree Debugger configured successfully!')
