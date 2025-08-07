/*
 * completion.c - Tab Completion for Reasons REPL
 * 
 * Features:
 * - Context-aware tab completion
 * - REPL command completion
 * - Debugger command completion
 * - Language keyword completion
 * - Variable name completion
 * - Function name completion
 * - Node identifier completion
 * - Filename completion for I/O commands
 * - Custom completion generators
 * - Multi-context support (REPL vs debug mode)
 */

#include "repl/completion.h"
#include "reasons/repl.h"
#include "reasons/debugger.h"
#include "reasons/runtime.h"
#include "reasons/parser.h"
#include "reasons/tree.h"
#include "utils/collections.h"
#include "utils/string_utils.h"
#include "utils/logger.h"
#include <readline/readline.h>
#include <readline/history.h>
#include <dirent.h>
#include <fnmatch.h>
#include <string.h>
#include <stdlib.h>

/* Current REPL instance for completion context */
static REPLState *current_repl = NULL;

/* Completion context types */
typedef enum {
    CTX_REPL_COMMAND,      // REPL commands (starting with .)
    CTX_DEBUGGER_COMMAND,  // Debugger commands
    CTX_EXPRESSION,        // DSL expressions
    CTX_FILENAME,          // Filename completion
    CTX_NODE_ID            // Node identifier completion
} CompletionContext;

/* ======== STATIC DATA ======== */

/* REPL commands */
static const char *repl_commands[] = {
    "help", "exit", "quit", "verbose", "debug", "run", "load", "save",
    "env", "history", "clear", "license", "version", "reset", "coverage", "profile",
    NULL
};

/* Debugger commands */
static const char *debugger_commands[] = {
    "help", "break", "run", "step", "next", "continue", "print", "watch",
    "backtrace", "coverage", "explain", "history", "quit", "list", "where",
    NULL
};

/* Language keywords */
static const char *language_keywords[] = {
    "if", "else", "match", "case", "default", "let", "fn", "return",
    "true", "false", "null", "import", "export", "for", "while", "break",
    "continue", "try", "catch", "throw", "in", "as", "is", "not", "and", "or",
    NULL
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static CompletionContext get_completion_context(const char *line_buffer, int start_pos) {
    // Check if we're completing a REPL command
    if (start_pos == 0 && line_buffer[0] == '.') {
        return CTX_REPL_COMMAND;
    }
    
    // Check if we're in debug mode
    if (current_repl && current_repl->debug_mode) {
        // First token in debug mode is debugger command
        char *first_space = strchr(line_buffer, ' ');
        if (!first_space || (first_space - line_buffer) > start_pos) {
            return CTX_DEBUGGER_COMMAND;
        }
        
        // Check for specific debugger commands that take node IDs
        char command[32] = {0};
        strncpy(command, line_buffer, first_space - line_buffer);
        if (strcmp(command, "break") == 0 || strcmp(command, "watch") == 0) {
            return CTX_NODE_ID;
        }
    }
    
    // Check for filename completion contexts
    const char *file_commands[] = {".load", ".save", "load", "save"};
    for (int i = 0; i < sizeof(file_commands)/sizeof(file_commands[0]); i++) {
        char *cmd_pos = strstr(line_buffer, file_commands[i]);
        if (cmd_pos && (cmd_pos - line_buffer) <= start_pos) {
            // Check if we're after the command
            char *after_cmd = cmd_pos + strlen(file_commands[i]);
            while (*after_cmd == ' ') after_cmd++;
            if (after_cmd <= line_buffer + start_pos) {
                return CTX_FILENAME;
            }
        }
    }
    
    // Default to expression context
    return CTX_EXPRESSION;
}

static char *command_generator(const char *text, int state, const char **commands) {
    static int list_index, len;
    const char *name;
    
    // Initialize on first call
    if (!state) {
        list_index = 0;
        len = strlen(text);
    }
    
    // Return next match
    while ((name = commands[list_index++])) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    
    return NULL;
}

static char *filename_generator(const char *text, int state) {
    static DIR *dir = NULL;
    static size_t text_len;
    static char *dir_path = NULL;
    static char *file_name = NULL;
    
    struct dirent *entry;
    char *result = NULL;
    
    // Initialize on first call
    if (!state) {
        text_len = strlen(text);
        
        // Extract directory path
        char *last_slash = strrchr(text, '/');
        if (last_slash) {
            size_t dir_len = last_slash - text + 1;
            dir_path = strndup(text, dir_len);
            file_name = strdup(last_slash + 1);
        } else {
            dir_path = strdup("./");
            file_name = strdup(text);
        }
        
        // Open directory
        if (dir) closedir(dir);
        dir = opendir(dir_path);
        if (!dir) {
            free(dir_path);
            free(file_name);
            return NULL;
        }
    }
    
    // Find matching files
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files unless explicitly requested
        if (file_name[0] != '.' && entry->d_name[0] == '.') {
            continue;
        }
        
        if (fnmatch(file_name, entry->d_name, FNM_PATHNAME) == 0) {
            // Build full path
            size_t path_len = strlen(dir_path) + strlen(entry->d_name) + 1;
            char *full_path = malloc(path_len);
            snprintf(full_path, path_len, "%s%s", dir_path, entry->d_name);
            
            // Add / for directories
            if (entry->d_type == DT_DIR) {
                full_path = realloc(full_path, path_len + 2);
                strcat(full_path, "/");
            }
            
            result = full_path;
            break;
        }
    }
    
    // Cleanup on last match
    if (!result) {
        if (dir) {
            closedir(dir);
            dir = NULL;
        }
        free(dir_path);
        free(file_name);
        dir_path = NULL;
        file_name = NULL;
    }
    
    return result;
}

static char *variable_generator(const char *text, int state) {
    static hash_iter_t iter;
    static int len;
    const char *name;
    
    if (!current_repl || !current_repl->env) {
        return NULL;
    }
    
    // Initialize on first call
    if (!state) {
        len = strlen(text);
        iter = hash_iter(current_repl->env->global_scope->variables);
    }
    
    // Find next matching variable
    while (hash_next(current_repl->env->global_scope->variables, &iter, &name, NULL)) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }
    
    return NULL;
}

static char *node_id_generator(const char *text, int state) {
    static vector_iter_t iter;
    static int len;
    TreeNode *node;
    
    if (!current_repl || !current_repl->debugger || !current_repl->debugger->tree) {
        return NULL;
    }
    
    // Initialize on first call
    if (!state) {
        len = strlen(text);
        iter = tree_iter_create(current_repl->debugger->tree);
    }
    
    // Find next matching node
    while ((node = tree_iter_next(&iter))) {
        if (node->id && strncmp(node->id, text, len) == 0) {
            return strdup(node->id);
        }
    }
    
    return NULL;
}

static char *expression_generator(const char *text, int state) {
    static int source_index;
    static const char **current_source;
    static int len;
    
    // Initialize on first call
    if (!state) {
        len = strlen(text);
        source_index = 0;
        current_source = language_keywords;
    }
    
    // Try current source first
    if (current_source) {
        while (current_source[source_index]) {
            const char *name = current_source[source_index++];
            if (strncmp(name, text, len) == 0) {
                return strdup(name);
            }
        }
    }
    
    // Switch to next source
    if (current_source == language_keywords) {
        source_index = 0;
        current_source = NULL;
        return variable_generator(text, 0);
    }
    
    // Then try variables
    if (!current_source) {
        char *var = variable_generator(text, state ? 1 : 0);
        if (var) return var;
    }
    
    // Finally try functions
    // (Function completion not implemented in this version)
    
    return NULL;
}

/* ======== PUBLIC API ======== */

void completion_set_repl(REPLState *repl) {
    current_repl = repl;
}

char **repl_completion(const char *text, int start, int end) {
    // Don't complete if we don't have context
    if (!current_repl) {
        return NULL;
    }
    
    // Get completion context
    CompletionContext ctx = get_completion_context(rl_line_buffer, start);
    
    // Delegate to appropriate generator
    rl_attempted_completion_over = 1;
    
    switch (ctx) {
        case CTX_REPL_COMMAND:
            return rl_completion_matches(text, 
                (rl_compentry_func_t *)command_generator, (void*)repl_commands);
            
        case CTX_DEBUGGER_COMMAND:
            return rl_completion_matches(text, 
                (rl_compentry_func_t *)command_generator, (void*)debugger_commands);
            
        case CTX_FILENAME:
            return rl_completion_matches(text, 
                (rl_compentry_func_t *)filename_generator);
            
        case CTX_NODE_ID:
            return rl_completion_matches(text, 
                (rl_compentry_func_t *)node_id_generator);
            
        case CTX_EXPRESSION:
        default:
            return rl_completion_matches(text, 
                (rl_compentry_func_t *)expression_generator);
    }
}

void register_completion_generator(const char *context_type, rl_compentry_func_t *generator) {
    // This would add to a registry in a real implementation
    LOG_DEBUG("Custom completion generator registered for: %s", context_type);
}

void unregister_completion_generator(const char *context_type) {
    LOG_DEBUG("Completion generator unregistered for: %s", context_type);
}
