/*
 * repl.c - Read-Eval-Print Loop (REPL) for Reasons DSL
 * 
 * Features:
 * - Interactive command-line interface
 * - Expression evaluation
 * - Variable assignment
 * - Multiline input support
 * - Tab completion
 * - Command history
 * - Integrated debugging
 * - Script loading
 * - Help system
 * - Customizable prompt
 * - Syntax highlighting
 */

#include "reasons/repl.h"
#include "reasons/eval.h"
#include "reasons/parser.h"
#include "reasons/lexer.h"
#include "reasons/runtime.h"
#include "reasons/debugger.h"
#include "repl/commands.h"
#include "repl/completion.h"
#include "repl/history.h"
#include "repl/prompt.h"
#include "utils/logger.h"
#include "utils/error.h"
#include "utils/memory.h"
#include "utils/string_utils.h"
#include "io/fileio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>

/* REPL state structure */
struct REPLState {
    runtime_env_t *env;          // Runtime environment
    eval_context_t *eval_ctx;     // Evaluation context
    DebuggerState *debugger;      // Debugger state
    bool running;                // REPL running state
    bool verbose;                // Verbose output
    bool debug_mode;             // Debugger integrated mode
    unsigned line_count;         // Input line counter
    char *last_error;            // Last error message
    char *current_script;        // Currently loaded script
    vector_t *input_buffer;      // For multiline input
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static void initialize_repl(REPLState *repl) {
    // Initialize GNU Readline
    rl_attempted_completion_function = repl_completion;
    using_history();
    stifle_history(1000);  // Limit history to 1000 entries
    
    // Load history from file
    history_load(HISTORY_FILE);
    
    // Set up custom prompt
    prompt_init(repl->env);
}

static void cleanup_repl(REPLState *repl) {
    // Save command history
    history_save(HISTORY_FILE);
    
    // Clean up input buffer
    if (repl->input_buffer) {
        for (size_t i = 0; i < vector_size(repl->input_buffer); i++) {
            char *line = vector_at(repl->input_buffer, i);
            mem_free(line);
        }
        vector_destroy(repl->input_buffer);
    }
    
    // Free other resources
    if (repl->last_error) mem_free(repl->last_error);
    if (repl->current_script) mem_free(repl->current_script);
}

static bool is_incomplete_input(const char *input) {
    if (!input) return false;
    
    // Check for unterminated structures
    int paren_count = 0;
    int brace_count = 0;
    int bracket_count = 0;
    char last_char = '\0';
    
    for (const char *p = input; *p; p++) {
        switch (*p) {
            case '(': paren_count++; break;
            case ')': paren_count--; break;
            case '{': brace_count++; break;
            case '}': brace_count--; break;
            case '[': bracket_count++; break;
            case ']': bracket_count--; break;
            case '"': 
                // Skip until closing quote
                while (*++p != '"' && *p != '\0') {
                    if (*p == '\\' && *(p+1) != '\0') p++;
                }
                if (*p == '\0') return true; // Unterminated string
                break;
            case '\'':
                // Skip until closing single quote
                while (*++p != '\'' && *p != '\0') {
                    if (*p == '\\' && *(p+1) != '\0') p++;
                }
                if (*p == '\0') return true;
                break;
        }
        last_char = *p;
    }
    
    // Check for trailing operators
    if (last_char == '+' || last_char == '-' || last_char == '*' || 
        last_char == '/' || last_char == '=' || last_char == '>' || 
        last_char == '<' || last_char == '&' || last_char == '|') {
        return true;
    }
    
    return paren_count > 0 || brace_count > 0 || bracket_count > 0;
}

static char *get_multiline_input(REPLState *repl, const char *prompt) {
    if (!repl->input_buffer) {
        repl->input_buffer = vector_create(4);
    }
    
    char *line = readline(prompt);
    if (!line) return NULL;  // EOF
    
    // Handle special REPL commands
    if (*line == '.' || *line == '!') {
        return line;
    }
    
    // Add to buffer
    vector_append(repl->input_buffer, line);
    
    // Check if input is complete
    if (!is_incomplete_input(line)) {
        // Combine all lines
        size_t total_len = 0;
        for (size_t i = 0; i < vector_size(repl->input_buffer); i++) {
            total_len += strlen(vector_at(repl->input_buffer, i)) + 1; // +1 for newline
        }
        
        char *full_input = mem_alloc(total_len + 1);
        if (!full_input) return NULL;
        
        *full_input = '\0';
        for (size_t i = 0; i < vector_size(repl->input_buffer); i++) {
            char *part = vector_at(repl->input_buffer, i);
            strcat(full_input, part);
            strcat(full_input, "\n");
            mem_free(part);
        }
        
        vector_clear(repl->input_buffer);
        return full_input;
    }
    
    // Continue input with secondary prompt
    return get_multiline_input(repl, "... ");
}

static void handle_input(REPLState *repl, const char *input) {
    if (!input || *input == '\0') return;
    
    repl->line_count++;
    
    // Handle REPL commands
    if (*input == '.') {
        handle_repl_command(repl, input + 1);
        return;
    }
    
    // Handle shell commands
    if (*input == '!') {
        system(input + 1);
        return;
    }
    
    // Handle debugger commands in debug mode
    if (repl->debug_mode) {
        // Pass directly to debugger
        debugger_handle_command(repl->debugger, input);
        return;
    }
    
    // Normal evaluation
    Lexer *lexer = lexer_create(input);
    if (!lexer) {
        LOG_ERROR("Failed to create lexer");
        return;
    }
    
    Parser *parser = parser_create(lexer);
    if (!parser) {
        LOG_ERROR("Failed to create parser");
        lexer_destroy(lexer);
        return;
    }
    
    AST_Node *node = parser_parse(parser);
    if (!node) {
        // Check for errors
        if (parser_has_error(parser)) {
            const char *error = parser_get_error(parser);
            if (repl->last_error) mem_free(repl->last_error);
            repl->last_error = string_duplicate(error);
            printf("Error: %s\n", error);
        }
        parser_destroy(parser);
        lexer_destroy(lexer);
        return;
    }
    
    // Evaluate the parsed AST
    reasons_value_t result = eval_node(repl->eval_ctx, node);
    
    // Print result unless it's a void expression
    if (result.type != VALUE_VOID) {
        printf("=> ");
        reasons_value_print(&result, stdout);
        printf("\n");
    }
    
    // Cleanup
    reasons_value_free(&result);
    ast_destroy(node);
    parser_destroy(parser);
    lexer_destroy(lexer);
}

static void print_welcome_banner() {
    printf("\n");
    printf("=============================================\n");
    printf("  Reasons DSL REPL - Version %s\n", REASONS_VERSION);
    printf("  Type .help for available commands\n");
    printf("  Type .exit or Ctrl+D to quit\n");
    printf("=============================================\n");
    printf("\n");
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

REPLState* repl_create(runtime_env_t *env, eval_context_t *eval_ctx) {
    REPLState *repl = mem_alloc(sizeof(REPLState));
    if (repl) {
        memset(repl, 0, sizeof(REPLState));
        repl->env = env;
        repl->eval_ctx = eval_ctx;
        repl->running = true;
        repl->verbose = true;
        repl->line_count = 0;
        
        // Create debugger instance
        repl->debugger = debugger_create(env, eval_ctx, NULL);
    }
    return repl;
}

void repl_destroy(REPLState *repl) {
    if (!repl) return;
    
    cleanup_repl(repl);
    
    if (repl->debugger) {
        debugger_destroy(repl->debugger);
    }
    
    mem_free(repl);
}

void repl_run(REPLState *repl) {
    if (!repl) return;
    
    initialize_repl(repl);
    print_welcome_banner();
    
    while (repl->running) {
        // Get dynamic prompt
        char *prompt = generate_prompt(repl);
        
        // Read input
        char *input = get_multiline_input(repl, prompt);
        mem_free(prompt);
        
        // Check for EOF (Ctrl+D)
        if (!input) {
            printf("\n");
            repl->running = false;
            continue;
        }
        
        // Skip empty lines
        if (*input == '\0') {
            mem_free(input);
            continue;
        }
        
        // Add to history (except multiline continuations)
        if (!repl->input_buffer || vector_size(repl->input_buffer) == 0) {
            add_history(input);
        }
        
        // Process input
        handle_input(repl, input);
        mem_free(input);
    }
}

void repl_set_verbose(REPLState *repl, bool verbose) {
    if (repl) repl->verbose = verbose;
}

bool repl_is_verbose(REPLState *repl) {
    return repl ? repl->verbose : false;
}

void repl_enter_debug_mode(REPLState *repl) {
    if (repl) {
        repl->debug_mode = true;
        printf("Entered debug mode. Type 'continue' to resume execution.\n");
    }
}

void repl_exit_debug_mode(REPLState *repl) {
    if (repl) {
        repl->debug_mode = false;
        printf("Exited debug mode.\n");
    }
}

bool repl_in_debug_mode(REPLState *repl) {
    return repl ? repl->debug_mode : false;
}

DebuggerState* repl_get_debugger(REPLState *repl) {
    return repl ? repl->debugger : NULL;
}

runtime_env_t* repl_get_env(REPLState *repl) {
    return repl ? repl->env : NULL;
}

eval_context_t* repl_get_eval_context(REPLState *repl) {
    return repl ? repl->eval_ctx : NULL;
}

void repl_load_script(REPLState *repl, const char *filename) {
    if (!repl || !filename) return;
    
    char *script = read_file(filename);
    if (!script) {
        printf("Error: Could not read file: %s\n", filename);
        return;
    }
    
    // Update current script
    if (repl->current_script) mem_free(repl->current_script);
    repl->current_script = string_duplicate(filename);
    
    printf("Loading script: %s\n", filename);
    
    // Process each line
    char *saveptr;
    char *line = strtok_r(script, "\n", &saveptr);
    while (line) {
        // Skip empty lines and comments
        if (*line != '\0' && *line != '#') {
            char *trimmed = string_trim(line);
            if (*trimmed != '\0') {
                // Handle input line
                handle_input(repl, trimmed);
            }
            mem_free(trimmed);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    
    mem_free(script);
}

void repl_print_help() {
    printf("REPL Commands:\n");
    printf("  .help       - Show this help message\n");
    printf("  .exit       - Exit the REPL\n");
    printf("  .verbose    - Toggle verbose output\n");
    printf("  .debug      - Enter debug mode\n");
    printf("  .run        - Run the current script\n");
    printf("  .load <file>- Load and execute a script\n");
    printf("  .save <file>- Save current session to file\n");
    printf("  .env        - Show current environment\n");
    printf("  .history    - Show command history\n");
    printf("  .clear      - Clear the screen\n");
    printf("  .license    - Show license information\n");
    printf("  .version    - Show version information\n");
    printf("\n");
    printf("Special Commands:\n");
    printf("  !<command>  - Execute shell command\n");
    printf("\n");
    printf("Debugger Commands (in debug mode):\n");
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

const char* repl_get_current_script(REPLState *repl) {
    return repl ? repl->current_script : NULL;
}

unsigned repl_get_line_count(REPLState *repl) {
    return repl ? repl->line_count : 0;
}

void repl_clear_error(REPLState *repl) {
    if (repl && repl->last_error) {
        mem_free(repl->last_error);
        repl->last_error = NULL;
    }
}

const char* repl_get_last_error(REPLState *repl) {
    return repl ? repl->last_error : NULL;
}
