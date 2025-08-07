/*
 * run.c - Script execution mode for Reasons DSL
 *
 * Features:
 * - Executes Reasons DSL scripts
 * - Command-line argument passing
 * - Environment variable access
 * - Runtime flags
 * - Error handling
 * - Execution time reporting
 * - Memory limits
 * - Sandbox mode
 */

#include "reasons/cli.h"
#include "reasons/runtime.h"
#include "reasons/vm.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/resource.h>

/* ======== FUNCTION PROTOTYPES ======== */

static void print_help();
static double get_time();

/* ======== PUBLIC API IMPLEMENTATION ======== */

int cli_run(int argc, char **argv) {
    if (argc < 2) {
        print_help();
        return EXIT_FAILURE;
    }

    // Parse command-line options
    bool show_time = false;
    bool debug_mode = false;
    bool sandbox = false;
    size_t memory_limit = 0; // 0 = unlimited
    const char *script_file = NULL;
    vector_t *script_args = vector_create(8);

    static struct option long_options[] = {
        {"time", no_argument, 0, 't'},
        {"debug", no_argument, 0, 'd'},
        {"sandbox", no_argument, 0, 's'},
        {"memory-limit", required_argument, 0, 'm'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "tdsm:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 't':
                show_time = true;
                break;
            case 'd':
                debug_mode = true;
                break;
            case 's':
                sandbox = true;
                break;
            case 'm': {
                char *end;
                memory_limit = strtoul(optarg, &end, 10);
                if (*end == 'M' || *end == 'm') {
                    memory_limit *= 1024 * 1024;
                } else if (*end == 'K' || *end == 'k') {
                    memory_limit *= 1024;
                }
                break;
            }
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

    // Set memory limit if specified
    if (memory_limit > 0) {
        struct rlimit limit;
        limit.rlim_cur = memory_limit;
        limit.rlim_max = memory_limit;
        if (setrlimit(RLIMIT_AS, &limit) {
            LOG_WARN("Failed to set memory limit");
        }
    }

    // Execute script
    RuntimeOptions options = {
        .script_file = script_file,
        .args = script_args,
        .debug_mode = debug_mode,
        .sandbox_mode = sandbox
    };

    double start_time = get_time();
    RuntimeResult result = execute_script(&options);
    double end_time = get_time();

    if (result.success) {
        if (show_time) {
            printf("Execution time: %.3f seconds\n", end_time - start_time);
        }
        LOG_INFO("Script executed successfully");
    } else {
        LOG_ERROR("Script execution failed: %s", result.error_message);
        if (result.line > 0) {
            LOG_ERROR("Error occurred at %s:%d", script_file, result.line);
        }
    }

    // Cleanup
    for (size_t i = 0; i < vector_size(script_args); i++) {
        mem_free(vector_at(script_args, i));
    }
    vector_destroy(script_args);

    return result.success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void print_help() {
    printf("Usage: reasons run [options] <script> [arguments]\n");
    printf("Execute a Reasons DSL script.\n\n");
    printf("Options:\n");
    printf("  -t, --time          Display execution time\n");
    printf("  -d, --debug         Enable debug mode\n");
    printf("  -s, --sandbox       Enable sandbox mode\n");
    printf("  -m, --memory-limit <size> Set memory limit (e.g., 100M, 1G)\n");
    printf("  -h, --help          Show this help message\n");
}

static double get_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}
