/*
 * compile.c - Batch compilation mode for Reasons DSL
 *
 * Features:
 * - Compiles one or more Reasons source files
 * - Outputs executable or intermediate representation
 * - Dependency resolution
 * - Optimization levels
 * - Cross-compilation support
 * - Error reporting with source locations
 * - Build configuration
 */

#include "reasons/cli.h"
#include "reasons/compiler.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/stat.h>

/* ======== CONSTANTS ======== */

#define DEFAULT_OUTPUT "a.out"

/* ======== FUNCTION PROTOTYPES ======== */

static void print_help();
static bool is_directory(const char *path);
static bool ensure_output_directory(const char *path);

/* ======== PUBLIC API IMPLEMENTATION ======== */

int cli_compile(int argc, char **argv) {
    if (argc < 2) {
        print_help();
        return EXIT_FAILURE;
    }

    // Parse command-line options
    const char *output_file = DEFAULT_OUTPUT;
    int optimization_level = 0;
    bool debug_info = false;
    bool warnings_as_errors = false;
    bool build_deps = true;
    const char *target_arch = NULL;
    vector_t *source_files = vector_create(8);
    vector_t *include_dirs = vector_create(4);
    vector_t *define_list = vector_create(4);

    static struct option long_options[] = {
        {"output", required_argument, 0, 'o'},
        {"optimize", optional_argument, 0, 'O'},
        {"debug", no_argument, 0, 'g'},
        {"warnings-as-errors", no_argument, 0, 'W'},
        {"include-dir", required_argument, 0, 'I'},
        {"define", required_argument, 0, 'D'},
        {"target", required_argument, 0, 't'},
        {"no-deps", no_argument, 0, 'n'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "o:O::gWI:D:t:nh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o':
                output_file = optarg;
                break;
            case 'O':
                if (optarg) {
                    optimization_level = atoi(optarg);
                } else {
                    optimization_level = 2;
                }
                break;
            case 'g':
                debug_info = true;
                break;
            case 'W':
                warnings_as_errors = true;
                break;
            case 'I':
                vector_append(include_dirs, string_dup(optarg));
                break;
            case 'D':
                vector_append(define_list, string_dup(optarg));
                break;
            case 't':
                target_arch = string_dup(optarg);
                break;
            case 'n':
                build_deps = false;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case '?':
                print_help();
                return EXIT_FAILURE;
        }
    }

    // Collect source files
    for (int i = optind; i < argc; i++) {
        if (is_directory(argv[i])) {
            // Recursively add all .reasons files in directory
            vector_t *files = file_list_directory(argv[i], true);
            for (size_t j = 0; j < vector_size(files); j++) {
                const char *path = vector_at(files, j);
                if (strstr(path, ".reasons")) {
                    vector_append(source_files, string_dup(path));
                }
            }
            vector_destroy_deep(files, mem_free);
        } else {
            vector_append(source_files, string_dup(argv[i]));
        }
    }

    if (vector_size(source_files) == 0) {
        LOG_ERROR("No input files specified");
        print_help();
        return EXIT_FAILURE;
    }

    // Ensure output directory exists
    if (!ensure_output_directory(output_file)) {
        LOG_ERROR("Failed to create output directory");
        return EXIT_FAILURE;
    }

    // Initialize compiler
    CompilerOptions options = {
        .output_file = output_file,
        .optimization_level = optimization_level,
        .debug_info = debug_info,
        .warnings_as_errors = warnings_as_errors,
        .build_dependencies = build_deps,
        .target_arch = target_arch,
        .source_files = source_files,
        .include_dirs = include_dirs,
        .definitions = define_list
    };

    CompilerResult result = compile(&options);
    if (result.success) {
        LOG_INFO("Compilation successful. Output: %s", output_file);
        LOG_DEBUG("Compilation stats: %d files, %d ms", 
                 result.files_compiled, result.time_ms);
    } else {
        LOG_ERROR("Compilation failed with %d errors", result.error_count);
    }

    // Cleanup
    for (size_t i = 0; i < vector_size(source_files); i++) {
        mem_free(vector_at(source_files, i));
    }
    vector_destroy(source_files);
    
    for (size_t i = 0; i < vector_size(include_dirs); i++) {
        mem_free(vector_at(include_dirs, i));
    }
    vector_destroy(include_dirs);
    
    for (size_t i = 0; i < vector_size(define_list); i++) {
        mem_free(vector_at(define_list, i));
    }
    vector_destroy(define_list);
    
    if (target_arch) mem_free((void*)target_arch);

    return result.success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void print_help() {
    printf("Usage: reasons compile [options] <source files or directories>\n");
    printf("Compile Reasons DSL source files into an executable.\n\n");
    printf("Options:\n");
    printf("  -o, --output <file>      Place the output into <file> (default: a.out)\n");
    printf("  -O, --optimize[=level]   Set optimization level (0-3, default: 2)\n");
    printf("  -g, --debug              Generate debug information\n");
    printf("  -W, --warnings-as-errors Treat warnings as errors\n");
    printf("  -I, --include-dir <dir>  Add directory to include search path\n");
    printf("  -D, --define <macro>     Define preprocessor macro\n");
    printf("  -t, --target <arch>      Set target architecture\n");
    printf("  -n, --no-deps            Skip dependency building\n");
    printf("  -h, --help               Show this help message\n");
}

static bool is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) return false;
    return S_ISDIR(st.st_mode);
}

static bool ensure_output_directory(const char *path) {
    char *dir = strdup(path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        bool result = ensure_directory_exists(dir);
        mem_free(dir);
        return result;
    }
    mem_free(dir);
    return true; // No directory component
}
