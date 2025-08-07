/*
 * commands.c - REPL Command Handlers for Reasons DSL
 * 
 * Features:
 * - Comprehensive command system for interactive environment
 * - Help system with detailed command documentation
 * - Script loading and execution
 * - Environment inspection
 * - History management
 * - Debugger integration
 * - Session saving/loading
 * - Configuration commands
 * - Error handling and user feedback
 */

#include "repl/commands.h"
#include "reasons/repl.h"
#include "reasons/runtime.h"
#include "reasons/eval.h"
#include "reasons/debugger.h"
#include "io/fileio.h"
#include "io/json_io.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include "utils/collections.h"
#include "utils/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Command handler function type */
typedef void (*CommandHandler)(REPLState *repl, const char *args);

/* Command structure */
typedef struct {
    const char *name;           // Command name
    CommandHandler handler;     // Handler function
    const char *help;           // Help text
    const char *usage;          // Usage example
} REPLCommand;

/* ======== FORWARD DECLARATIONS ======== */
static void cmd_help(REPLState *repl, const char *args);
static void cmd_exit(REPLState *repl, const char *args);
static void cmd_verbose(REPLState *repl, const char *args);
static void cmd_debug(REPLState *repl, const char *args);
static void cmd_run(REPLState *repl, const char *args);
static void cmd_load(REPLState *repl, const char *args);
static void cmd_save(REPLState *repl, const char *args);
static void cmd_env(REPLState *repl, const char *args);
static void cmd_history(REPLState *repl, const char *args);
static void cmd_clear(REPLState *repl, const char *args);
static void cmd_license(REPLState *repl, const char *args);
static void cmd_version(REPLState *repl, const char *args);
static void cmd_reset(REPLState *repl, const char *args);
static void cmd_coverage(REPLState *repl, const char *args);
static void cmd_profile(REPLState *repl, const char *args);

/* Command table */
static const REPLCommand commands[] = {
    {"help", cmd_help, "Show help information", ".help [command]"},
    {"exit", cmd_exit, "Exit the REPL", ".exit"},
    {"quit", cmd_exit, "Exit the REPL (alias for .exit)", ".quit"},
    {"verbose", cmd_verbose, "Toggle verbose output mode", ".verbose"},
    {"debug", cmd_debug, "Enter or exit debug mode", ".debug [on|off]"},
    {"run", cmd_run, "Run the current script", ".run"},
    {"load", cmd_load, "Load and execute a script file", ".load <filename>"},
    {"save", cmd_save, "Save current session to file", ".save <filename>"},
    {"env", cmd_env, "Show current environment state", ".env [filter]"},
    {"history", cmd_history, "Show command history", ".history [count]"},
    {"clear", cmd_clear, "Clear the screen", ".clear"},
    {"license", cmd_license, "Show license information", ".license"},
    {"version", cmd_version, "Show version information", ".version"},
    {"reset", cmd_reset, "Reset the environment", ".reset"},
    {"coverage", cmd_coverage, "Show coverage information", ".coverage"},
    {"profile", cmd_profile, "Show performance profile", ".profile"},
    {NULL, NULL, NULL, NULL} // Sentinel
};

/* ======== COMMAND HANDLERS ======== */

static void cmd_help(REPLState *repl, const char *args) {
    printf("Available REPL commands:\n");
    for (int i = 0; commands[i].name; i++) {
        printf("  .%-10s - %s\n", commands[i].name, commands[i].help);
        
        // Show detailed usage if requested
        if (args && *args && strcmp(commands[i].name, args) == 0) {
            printf("      Usage: %s\n", commands[i].usage);
        }
    }
    
    printf("\nSpecial commands:\n");
    printf("  !<command>    - Execute shell command\n");
    printf("  <expression>  - Evaluate Reasons expression\n");
    
    if (repl_in_debug_mode(repl)) {
        printf("\nDebugger commands:\n");
        printf("  break       - Set breakpoint\n");
        printf("  run         - Start execution\n");
        printf("  step        - Step into next node\n");
        printf("  next        - Step over next node\n");
        printf("  continue    - Continue execution\n");
        printf("  print       - Print expression\n");
        printf("  watch       - Add watch expression\n");
        printf("  backtrace   - Show call stack\n");
        printf("  coverage    - Show coverage info\n");
        printf("  explain     - Explain current decision\n");
        printf("  history     - Show decision history\n");
        printf("  quit        - Exit debugger\n");
    }
}

static void cmd_exit(REPLState *repl, const char *args) {
    repl->running = false;
    printf("Exiting REPL. Goodbye!\n");
}

static void cmd_verbose(REPLState *repl, const char *args) {
    bool new_verbose = !repl->verbose;
    repl_set_verbose(repl, new_verbose);
    printf("Verbose mode %s\n", new_verbose ? "ON" : "OFF");
}

static void cmd_debug(REPLState *repl, const char *args) {
    if (args && *args) {
        if (strcmp(args, "on") == 0) {
            repl_enter_debug_mode(repl);
        } else if (strcmp(args, "off") == 0) {
            repl_exit_debug_mode(repl);
        } else {
            printf("Usage: .debug [on|off]\n");
        }
    } else {
        // Toggle debug mode
        if (repl_in_debug_mode(repl)) {
            repl_exit_debug_mode(repl);
        } else {
            repl_enter_debug_mode(repl);
        }
    }
}

static void cmd_run(REPLState *repl, const char *args) {
    const char *script = repl_get_current_script(repl);
    if (!script) {
        printf("No current script loaded. Use '.load' first.\n");
        return;
    }
    
    printf("Running script: %s\n", script);
    repl_load_script(repl, script);
}

static void cmd_load(REPLState *repl, const char *args) {
    if (!args || *args == '\0') {
        printf("Usage: .load <filename>\n");
        return;
    }
    
    repl_load_script(repl, args);
}

static void cmd_save(REPLState *repl, const char *args) {
    if (!args || *args == '\0') {
        printf("Usage: .save <filename>\n");
        return;
    }
    
    // Get history
    vector_t *history = history_get_all();
    if (!history || vector_size(history) == 0) {
        printf("No history to save\n");
        return;
    }
    
    // Open file
    FILE *f = fopen(args, "w");
    if (!f) {
        printf("Error: Could not open file for writing: %s\n", args);
        return;
    }
    
    // Write session header
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm);
    
    fprintf(f, "# Reasons REPL Session - Saved at %s\n", time_buf);
    if (repl->current_script) {
        fprintf(f, "# Current script: %s\n", repl->current_script);
    }
    fprintf(f, "# Line count: %u\n\n", repl->line_count);
    
    // Write history
    for (size_t i = 0; i < vector_size(history); i++) {
        const char *entry = vector_at(history, i);
        fprintf(f, "%s\n", entry);
    }
    
    fclose(f);
    printf("Session saved to: %s (%zu commands)\n", args, vector_size(history));
}

static void cmd_env(REPLState *repl, const char *args) {
    runtime_env_t *env = repl_get_env(repl);
    if (!env) {
        printf("No active environment\n");
        return;
    }
    
    const char *filter = args && *args ? args : NULL;
    
    printf("Environment State:\n");
    printf("==================\n");
    
    // Show variables
    printf("\nVariables:\n");
    // (Implementation would iterate through scopes)
    printf("  [Variable inspection not implemented]\n");
    
    // Show functions
    printf("\nFunctions:\n");
    // (Implementation would iterate through function registry)
    printf("  [Function inspection not implemented]\n");
    
    // Show configuration
    printf("\nConfiguration:\n");
    bool golf_mode = *(bool*)runtime_get_option(env, RUNTIME_OPTION_GOLF_MODE);
    unsigned max_recursion = *(unsigned*)runtime_get_option(env, RUNTIME_OPTION_MAX_RECURSION);
    bool tracing = *(bool*)runtime_get_option(env, RUNTIME_OPTION_TRACING);
    bool explanations = *(bool*)runtime_get_option(env, RUNTIME_OPTION_EXPLANATIONS);
    
    printf("  Golf mode:        %s\n", golf_mode ? "ON" : "OFF");
    printf("  Max recursion:    %u\n", max_recursion);
    printf("  Tracing:          %s\n", tracing ? "ON" : "OFF");
    printf("  Explanations:     %s\n", explanations ? "ON" : "OFF");
}

static void cmd_history(REPLState *repl, const char *args) {
    int max_entries = 10; // Default
    if (args && *args != '\0') {
        max_entries = atoi(args);
        if (max_entries <= 0) max_entries = 10;
    }
    
    vector_t *history = history_get_all();
    if (!history || vector_size(history) == 0) {
        printf("No command history\n");
        return;
    }
    
    size_t start = vector_size(history) > (size_t)max_entries ? 
        vector_size(history) - max_entries : 0;
    
    printf("Command History (last %d entries):\n", max_entries);
    for (size_t i = start; i < vector_size(history); i++) {
        printf("%5zu: %s\n", i + 1, (const char*)vector_at(history, i));
    }
}

static void cmd_clear(REPLState *repl, const char *args) {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

static void cmd_license(REPLState *repl, const char *args) {
    printf("\nReasons DSL - License Information\n");
    printf("=================================\n");
    printf("Reasons is licensed under the MIT License.\n\n");
    printf("Permission is hereby granted, free of charge, to any person obtaining a copy\n");
    printf("of this software and associated documentation files (the \"Software\"), to deal\n");
    printf("in the Software without restriction, including without limitation the rights\n");
    printf("to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n");
    printf("copies of the Software, and to permit persons to whom the Software is\n");
    printf("furnished to do so, subject to the following conditions:\n\n");
    printf("The above copyright notice and this permission notice shall be included in all\n");
    printf("copies or substantial portions of the Software.\n\n");
    printf("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n");
    printf("IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n");
    printf("FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n");
    printf("AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n");
    printf("LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n");
    printf("OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n");
    printf("SOFTWARE.\n");
}

static void cmd_version(REPLState *repl, const char *args) {
    printf("Reasons DSL\n");
    printf("Version: %s\n", REASONS_VERSION);
    printf("Build date: %s\n", __DATE__);
}

static void cmd_reset(REPLState *repl, const char *args) {
    printf("Resetting environment...\n");
    
    // Destroy and recreate environment
    runtime_env_t *old_env = repl->env;
    eval_context_t *old_ctx = repl->eval_ctx;
    
    // Create new environment
    repl->env = runtime_create();
    repl->eval_ctx = eval_context_create(repl->env);
    
    // Update debugger with new environment
    if (repl->debugger) {
        debugger_destroy(repl->debugger);
        repl->debugger = debugger_create(repl->env, repl->eval_ctx, NULL);
    }
    
    // Destroy old environment
    if (old_env) runtime_destroy(old_env);
    if (old_ctx) eval_context_destroy(old_ctx);
    
    // Clear script reference
    if (repl->current_script) {
        mem_free(repl->current_script);
        repl->current_script = NULL;
    }
    
    // Reset line count
    repl->line_count = 0;
    
    printf("Environment reset complete\n");
}

static void cmd_coverage(REPLState *repl, const char *args) {
    if (!repl->debugger) {
        printf("Debugger not initialized\n");
        return;
    }
    
    debugger_print_coverage(repl->debugger);
}

static void cmd_profile(REPLState *repl, const char *args) {
    printf("Performance profiling not implemented yet\n");
}

/* ======== PUBLIC API ======== */

void handle_repl_command(REPLState *repl, const char *input) {
    if (!repl || !input || *input == '\0') {
        return;
    }
    
    // Parse command and arguments
    char *cmd = string_trim(string_duplicate(input));
    char *args = strchr(cmd, ' ');
    if (args) {
        *args = '\0'; // Terminate command string
        args = string_trim(args + 1); // Arguments start after space
    }
    
    // Find and execute command
    bool found = false;
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            commands[i].handler(repl, args);
            found = true;
            break;
        }
    }
    
    if (!found) {
        printf("Unknown command: .%s. Type '.help' for available commands.\n", cmd);
    }
    
    mem_free(cmd);
}

void register_custom_command(const char *name, CommandHandler handler, 
                            const char *help, const char *usage) {
    // This would add to the command table in a real implementation
    LOG_INFO("Custom command registration not implemented: %s", name);
}

void unregister_custom_command(const char *name) {
    LOG_INFO("Custom command unregistration not implemented: %s", name);
}
