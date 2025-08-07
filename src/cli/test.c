/*
 * test.c - Test runner CLI for Reasons DSL
 *
 * Features:
 * - Runs test suites
 * - Supports filtering by test name
 * - Output formats (plain, JUnit, TAP)
 * - Code coverage reporting
 * - Parallel test execution
 * - Test fixtures
 * - Failure reporting
 */

#include "reasons/cli.h"
#include "reasons/test_runner.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>

/* ======== FUNCTION PROTOTYPES ======== */

static void print_help();
static void print_test_summary(TestRunnerResult *result, double duration);

/* ======== PUBLIC API IMPLEMENTATION ======== */

int cli_test(int argc, char **argv) {
    // Default options
    const char *filter = NULL;
    TestOutputFormat format = TEST_OUTPUT_PLAIN;
    const char *output_file = NULL;
    bool coverage = false;
    int jobs = 1;
    bool list_tests = false;
    bool fail_fast = false;
    vector_t *test_paths = vector_create(8);

    static struct option long_options[] = {
        {"filter", required_argument, 0, 'f'},
        {"format", required_argument, 0, 'F'},
        {"output", required_argument, 0, 'o'},
        {"coverage", no_argument, 0, 'c'},
        {"jobs", required_argument, 0, 'j'},
        {"list", no_argument, 0, 'l'},
        {"fail-fast", no_argument, 0, 'x'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "f:F:o:cj:lxh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'f':
                filter = optarg;
                break;
            case 'F':
                if (strcmp(optarg, "plain") == 0) {
                    format = TEST_OUTPUT_PLAIN;
                } else if (strcmp(optarg, "junit") == 0) {
                    format = TEST_OUTPUT_JUNIT;
                } else if (strcmp(optarg, "tap") == 0) {
                    format = TEST_OUTPUT_TAP;
                } else {
                    LOG_ERROR("Unknown output format: %s", optarg);
                    print_help();
                    return EXIT_FAILURE;
                }
                break;
            case 'o':
                output_file = optarg;
                break;
            case 'c':
                coverage = true;
                break;
            case 'j':
                jobs = atoi(optarg);
                if (jobs < 1) jobs = 1;
                break;
            case 'l':
                list_tests = true;
                break;
            case 'x':
                fail_fast = true;
                break;
            case 'h':
                print_help();
                return EXIT_SUCCESS;
            case '?':
                print_help();
                return EXIT_FAILURE;
        }
    }

    // Collect test paths
    for (int i = optind; i < argc; i++) {
        vector_append(test_paths, string_dup(argv[i]));
    }

    if (vector_size(test_paths) == 0) {
        // Default to current directory
        vector_append(test_paths, string_dup("."));
    }

    // Set up test runner
    TestRunnerOptions options = {
        .test_paths = test_paths,
        .filter = filter,
        .format = format,
        .output_file = output_file,
        .coverage = coverage,
        .jobs = jobs,
        .list_tests = list_tests,
        .fail_fast = fail_fast
    };

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    TestRunnerResult result = run_tests(&options);
    
    gettimeofday(&end_time, NULL);
    double duration = (end_time.tv_sec - start_time.tv_sec) + 
                     (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    print_test_summary(&result, duration);

    // Cleanup
    for (size_t i = 0; i < vector_size(test_paths); i++) {
        mem_free(vector_at(test_paths, i));
    }
    vector_destroy(test_paths);

    return result.fail_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void print_help() {
    printf("Usage: reasons test [options] [paths]\n");
    printf("Run Reasons DSL test suites.\n\n");
    printf("Options:\n");
    printf("  -f, --filter <pattern>  Only run tests matching pattern\n");
    printf("  -F, --format <format>   Set output format (plain, junit, tap)\n");
    printf("  -o, --output <file>     Write output to file\n");
    printf("  -c, --coverage          Collect code coverage information\n");
    printf("  -j, --jobs <n>          Number of parallel jobs (default: 1)\n");
    printf("  -l, --list              List tests without running them\n");
    printf("  -x, --fail-fast         Stop after first failure\n");
    printf("  -h, --help              Show this help message\n");
}

static void print_test_summary(TestRunnerResult *result, double duration) {
    printf("\nTest Summary:\n");
    printf("  Duration: %.3f seconds\n", duration);
    printf("  Total:    %d\n", result->total_count);
    printf("  Passed:   %d\n", result->pass_count);
    printf("  Failed:   %d\n", result->fail_count);
    printf("  Skipped:  %d\n", result->skip_count);
    
    if (result->fail_count > 0) {
        printf("\nFailed tests:\n");
        for (size_t i = 0; i < vector_size(result->failures); i++) {
            TestFailure *failure = vector_at(result->failures, i);
            printf("  %s: %s\n", failure->test_name, failure->message);
            if (failure->location) {
                printf("    at %s:%d\n", failure->location->file, failure->location->line);
            }
        }
    }
    
    if (result->coverage_report) {
        printf("\nCode Coverage: %.1f%%\n", result->coverage_percent);
    }
}
