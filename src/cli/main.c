/*
 * main.c - Main CLI entry point for Reasons DSL
 *
 * Features:
 * - Command-line argument parsing
 * - Subcommand dispatch (compile, run, debug, test)
 * - Help system
 * - Version information
 * - Error handling
 * - Configuration loading
 */

#include "reasons/cli.h"
#include "reasons/version.h"
#include "reasons/io/config.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

/* ======== STRUCTURE DEFINITIONS ======== */

typedef struct {
    const char *name;
    int (*func)(int argc, char **argv);
    const char *description;
} Command;

/* ======== GLOBAL VARIABLES ======== */

static ConfigManager *config = NULL;
static bool verbose_mode = false;

/* ======== COMMAND LIST ======== */

static Command commands[] = {
    {"compile", cli_compile, "Compile Reasons DSL source files"},
    {"run", cli_run, "Execute a Reasons DSL script"},
    {"debug", cli_debug, "Debug Reasons DSL programs interactively"},
    {"test", cli_test, "Run Reasons DSL test suites"},
    {NULL, NULL, NULL}
};

/* ======== FUNCTION PROTOTYPES ======== */

static void initialize();
static void cleanup();
static void print_help();
static void print_version();
static Command* find_command(const char *name);

/* ======== MAIN IMPLEMENTATION ======== */

int main(int argc, char **argv) {
    initialize();
    
    if (argc < 2) {
        print_help();
        cleanup();
        return EXIT_FAILURE;
    }

    // Handle global options
    int global_opt;
    while ((global_opt = getopt(argc, argv, "hvV")) != -1) {
        switch (global_opt) {
            case 'h':
                print_help();
                cleanup();
                return EXIT_SUCCESS;
            case 'v':
                verbose_mode = true;
                logger_set_level(LOG_LEVEL_DEBUG);
                break;
            case 'V':
                print_version();
                cleanup();
                return EXIT_SUCCESS;
            case '?':
                print_help();
                cleanup();
                return EXIT_FAILURE;
        }
    }

    Command *cmd = find_command(argv[optind]);
    if (!cmd) {
        LOG_ERROR("Unknown command: %s", argv[optind]);
        print_help();
        cleanup();
        return EXIT_FAILURE;
    }

    // Call the command function with the remaining arguments
    return cmd->func(argc - optind, argv + optind);
}

static void initialize() {
    // Initialize logger
    logger_init(LOG_LEVEL_INFO, stderr);
    
    // Load configuration
    config = config_manager_create();
    config_load_defaults(config);
    
    // Set log level from config
    if (config_get_bool(config, "verbose", false)) {
        logger_set_level(LOG_LEVEL_DEBUG);
    }
}

static void cleanup() {
    if (config) {
        config_manager_free(config);
    }
    logger_cleanup();
}

static Command* find_command(const char *name) {
    for (Command *cmd = commands; cmd->name; cmd++) {
        if (strcmp(cmd->name, name) == 0) {
            return cmd;
        }
    }
    return NULL;
}

static void print_help() {
    printf("Reasons DSL v%s\n", REASONS_VERSION);
    printf("Usage: reasons [global-options] <command> [command-options] [arguments]\n\n");
    printf("Global options:\n");
    printf("  -h, --help      Show this help message\n");
    printf("  -v, --verbose   Enable verbose output\n");
    printf("  -V, --version   Show version information\n\n");
    printf("Available commands:\n");
    
    for (Command *cmd = commands; cmd->name; cmd++) {
        printf("  %-10s %s\n", cmd->name, cmd->description);
    }
    
    printf("\nUse 'reasons <command> --help' for command-specific help\n");
}

static void print_version() {
    printf("Reasons DSL v%s\n", REASONS_VERSION);
    printf("Build date: %s\n", __DATE__);
    printf("Copyright (c) 2025 Aitzaz Imtiaz. All rights reserved.\n");
}
