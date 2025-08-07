/*
 * watch.c - Watch expression implementation for Reasons debugger
 * 
 * Features:
 * - Add/remove watch expressions
 * - Track values of expressions during execution
 * - Detect value changes during stepping
 * - Support for both simple variables and complex expressions
 * - Integration with debugger's execution flow
 */

#include "reasons/debugger.h"
#include "reasons/parser.h"
#include "reasons/eval.h"
#include "utils/error.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "utils/memory.h"
#include <string.h>
#include <stdio.h>

/* Internal helper functions */
static WatchExpr* create_watch_expr(const char *expr);
static bool watch_value_changed(WatchExpr *we, reasons_value_t *new_value);
static void update_watch_value(WatchExpr *we, reasons_value_t *new_value);

/* ======== WATCH EXPRESSION MANAGEMENT ======== */

WatchExpr* debugger_add_watch(DebuggerState *dbg, const char *expr) {
    if (!dbg || !expr || *expr == '\0') {
        LOG_ERROR("Invalid arguments for debugger_add_watch");
        return NULL;
    }

    // Create a new watch expression
    WatchExpr *we = create_watch_expr(expr);
    if (!we) {
        LOG_ERROR("Failed to create watch expression: %s", expr);
        return NULL;
    }

    // Evaluate initial value
    reasons_value_t initial_value = eval_node(dbg->eval_ctx, we->parsed_expr);
    if (runtime_last_error(dbg->env->runtime) {
        LOG_WARN("Error evaluating watch expression: %s", 
                runtime_error_message(dbg->env->runtime));
        reasons_value_free(&initial_value);
    } else {
        we->last_value = initial_value;
    }

    // Add to debugger's watch list
    if (!vector_append(dbg->watch_exprs, we)) {
        LOG_ERROR("Failed to add watch to debugger");
        if (we->expr) mem_free(we->expr);
        if (we->parsed_expr) ast_destroy(we->parsed_expr);
        mem_free(we);
        return NULL;
    }

    LOG_DEBUG("Added watch expression: %s", expr);
    return we;
}

bool debugger_remove_watch(DebuggerState *dbg, size_t index) {
    if (!dbg || index >= vector_size(dbg->watch_exprs)) {
        LOG_ERROR("Invalid watch index: %zu", index);
        return false;
    }

    WatchExpr *we = vector_at(dbg->watch_exprs, index);
    vector_remove(dbg->watch_exprs, index);
    
    // Cleanup watch resources
    if (we->expr) mem_free(we->expr);
    if (we->parsed_expr) ast_destroy(we->parsed_expr);
    reasons_value_free(&we->last_value);
    mem_free(we);
    
    LOG_DEBUG("Removed watch expression at index: %zu", index);
    return true;
}

void debugger_clear_watches(DebuggerState *dbg) {
    if (!dbg) return;
    
    for (size_t i = 0; i < vector_size(dbg->watch_exprs); i++) {
        WatchExpr *we = vector_at(dbg->watch_exprs, i);
        if (we->expr) mem_free(we->expr);
        if (we->parsed_expr) ast_destroy(we->parsed_expr);
        reasons_value_free(&we->last_value);
        mem_free(we);
    }
    vector_clear(dbg->watch_exprs);
    LOG_DEBUG("Cleared all watch expressions");
}

void debugger_update_watches(DebuggerState *dbg) {
    if (!dbg || !dbg->eval_ctx) return;

    for (size_t i = 0; i < vector_size(dbg->watch_exprs); i++) {
        WatchExpr *we = vector_at(dbg->watch_exprs, i);
        if (!we || !we->parsed_expr) continue;

        // Clear any previous errors
        runtime_clear_error(dbg->env->runtime);
        
        // Evaluate the expression
        reasons_value_t new_value = eval_node(dbg->eval_ctx, we->parsed_expr);
        
        // Handle evaluation errors
        if (runtime_last_error(dbg->env->runtime)) {
            LOG_DEBUG("Watch evaluation error: %s", 
                     runtime_error_message(dbg->env->runtime));
            reasons_value_free(&new_value);
            continue;
        }
        
        // Check if value has changed
        if (watch_value_changed(we, &new_value)) {
            // Update debugger UI
            if (dbg->verbose) {
                printf("Watch %zu: %s changed from ", i, we->expr);
                reasons_value_print(&we->last_value, stdout);
                printf(" to ");
                reasons_value_print(&new_value, stdout);
                printf("\n");
            }
            
            // Update stored value
            update_watch_value(we, &new_value);
        } else {
            reasons_value_free(&new_value);
        }
    }
}

/* ======== HELPER FUNCTIONS ======== */

static WatchExpr* create_watch_expr(const char *expr) {
    if (!expr) return NULL;

    // Create lexer and parser
    Lexer *lexer = lexer_create(expr);
    if (!lexer) {
        LOG_ERROR("Failed to create lexer for: %s", expr);
        return NULL;
    }
    
    Parser *parser = parser_create(lexer);
    if (!parser) {
        LOG_ERROR("Failed to create parser for: %s", expr);
        lexer_destroy(lexer);
        return NULL;
    }
    
    // Parse the expression
    AST_Node *parsed_expr = parser_parse_expression(parser);
    if (!parsed_expr) {
        LOG_ERROR("Failed to parse expression: %s", expr);
        parser_destroy(parser);
        lexer_destroy(lexer);
        return NULL;
    }
    
    // Create watch expression
    WatchExpr *we = mem_alloc(sizeof(WatchExpr));
    if (!we) {
        LOG_ERROR("Memory allocation failed for watch expression");
        ast_destroy(parsed_expr);
        parser_destroy(parser);
        lexer_destroy(lexer);
        return NULL;
    }
    
    // Initialize watch expression
    we->expr = string_duplicate(expr);
    we->parsed_expr = parsed_expr;
    we->last_value.type = VALUE_NULL;
    
    // Cleanup parser resources
    parser_destroy(parser);
    lexer_destroy(lexer);
    
    return we;
}

static bool watch_value_changed(WatchExpr *we, reasons_value_t *new_value) {
    if (!we || !new_value) return false;
    
    // Handle NULL values
    if (we->last_value.type == VALUE_NULL && new_value->type == VALUE_NULL) {
        return false;
    }
    if (we->last_value.type == VALUE_NULL || new_value->type == VALUE_NULL) {
        return true;
    }
    
    // Type-specific comparisons
    switch (we->last_value.type) {
        case VALUE_BOOL:
            if (new_value->type == VALUE_BOOL) {
                return we->last_value.data.bool_val != new_value->data.bool_val;
            }
            return true;
            
        case VALUE_NUMBER:
            if (new_value->type == VALUE_NUMBER) {
                return we->last_value.data.number_val != new_value->data.number_val;
            }
            return true;
            
        case VALUE_STRING:
            if (new_value->type == VALUE_STRING) {
                return strcmp(we->last_value.data.string_val, 
                             new_value->data.string_val) != 0;
            }
            return true;
            
        default:
            // For other types, consider them changed
            return true;
    }
}

static void update_watch_value(WatchExpr *we, reasons_value_t *new_value) {
    if (!we || !new_value) return;
    
    // Free previous value
    reasons_value_free(&we->last_value);
    
    // Clone the new value
    we->last_value = reasons_value_clone(new_value);
}
