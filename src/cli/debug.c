/*
 * debug.c - Debug mode entry for Reasons DSL
 *
 * Features:
 * - Interactive debugger
 * - Breakpoints
 * - Step execution
 * - Variable inspection
 * - Call stack viewing
 * - Watch expressions
 * - Disassembly
 * - Execution tracing
 */

#include "reasons/cli.h"
#include "reasons/debugger.h"
#include "reasons/vm.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <readline/readline.h>
#include <readline/history.h>

/* ======== FUNCTION PROTOTYPES ======== */

static void print_help();
static void initialize_readline();
static void cleanup_readline();

/* ======== PUBLIC API IMPLEMENTATION ======== */

int cli_debug(int argc, char **argv) {
    if (argc < 2) {
        print_help();
        return EXIT_FAILURE;
    }

    // Parse command-line options
    const char *script_file = NULL;
    vector_t *script_args = vector_create(8);
    bool break_on_start = false;
    const char *breakpoints = NULL;
    const char *watch_expressions = NULL;
    bool trace_execution = false;

    static struct option long_options[] = {
        {"break", optional_argument, 0, 'b'},
        {"breakpoint", required_argument, 0, 'B'},
        {"watch", required_argument, 0, 'w'},
        {"trace", no_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "b::B:w:th", long_options, NULL)) != -1) {
        switch (opt) {
            case 'b':
                break_on_start = true;
                if (optarg) {
                    breakpoints = optarg;
                }
                break;
            case 'B':
                breakpoints = optarg;
                break;
            case 'w':
                watch_expressions = optarg;
                break;
            case 't':
                trace_execution = true;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case '?':
                print_help();
                return EXIT_FAILURE;
        }
    }

    // Get script file
    if (optind >= argc) {
        LOG_ERROR("No script file specified");
        print_help();
        return EXIT_FAILURE;
    }
    script_file = argv[optind++];

    // Collect script arguments
    for (int i = optind; i < argc; i++) {
        vector_append(script_args, string_dup(argv[i]));
    }

    // Initialize debugger
    DebuggerOptions options = {
        .script_file = script_file,
        .args = script_args,
        .break_on_start = break_on_start,
        .initial_breakpoints = breakpoints,
        .watch_expressions = watch_expressions,
        .trace_execution = trace_execution
    };

    initialize_readline();
    DebuggerResult result = start_debugger(&options);
    cleanup_readline();
    
    if (!result.success) {
        LOG_ERROR("Debugger failed: %s", result.error_message);
    }

    // Cleanup
    for (size_t i = 0; i < vector_size(script_args); i++) {
        mem_free(vector_at(script_args, i));
    }
    vector_destroy(script_args);

    return result.success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void print_help() {
    printf("Usage: reasons debug [options] <script> [arguments]\n");
    printf("Debug Reasons DSL programs interactively.\n\n");
    printf("Options:\n");
    printf("  -b, --break[=location]  Break at the first instruction or specified location\n");
    printf("  -B, --breakpoint <list> Set initial breakpoints (comma-separated)\n");
    printf("  -w, --watch <exprs>     Set watch expressions (comma-separated)\n");
    printf("  -t, --trace             Enable execution tracing\n");
    printf("  -h, --help              Show this help message\n");
}

static void initialize_readline() {
    // Allow conditional parsing of the ~/.inputrc file
    rl_readline_name = "reasons";
    
    // Enable history
    using_history();
    
    // Set up completion
    rl_attempted_completion_function = debugger_completion;
}

static void cleanup_readline() {
    clear_history();
}
