/*
 * runtime.c - Runtime Environment for Reasons DSL
 * 
 * Features:
 * - Scoped variable management
 * - Function registry with built-in/stdlib support
 * - Consequence execution with side effects
 * - Execution context management
 * - Error handling and stack traces
 * - Garbage collection and memory management
 * - Configuration options (golf mode, tracing, etc.)
 */

#include "reasons/runtime.h"
#include "reasons/tree.h"
#include "reasons/eval.h"
#include "reasons/ast.h"
#include "stdlib/math.h"
#include "stdlib/string.h"
#include "stdlib/stats.h"
#include "stdlib/datetime.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "utils/error.h"
#include <string.h>
#include <time.h>

/* Execution scope structure */
typedef struct Scope {
    hash_table_t *variables;   // Variables in this scope
    struct Scope *parent;      // Parent scope
} Scope;

/* Function registry entry */
typedef struct {
    runtime_function_t function;
    char *description;
    unsigned min_args;
    unsigned max_args;
} FunctionEntry;

/* Runtime environment structure */
struct runtime_env {
    Scope *current_scope;      // Current variable scope
    hash_table_t *functions;   // Registered functions
    vector_t *call_stack;      // Function call stack
    vector_t *consequence_handlers; // Consequence handlers
    config_t config;           // Runtime configuration
    runtime_stats_t stats;     // Execution statistics
    error_t last_error;        // Last error code
    char *error_message;       // Detailed error message
    clock_t start_time;        // For timing measurements
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static Scope* scope_create(Scope *parent) {
    Scope *scope = mem_alloc(sizeof(Scope));
    if (scope) {
        scope->variables = hash_create(16);
        scope->parent = parent;
    }
    return scope;
}

static void scope_destroy(Scope *scope) {
    if (!scope) return;
    
    // Free variable names and values
    const char *key;
    reasons_value_t *value;
    hash_iter_t iter = hash_iter(scope->variables);
    while (hash_next(scope->variables, &iter, &key, (void**)&value)) {
        reasons_value_free(value);
        mem_free((void*)key);
        mem_free(value);
    }
    hash_destroy(scope->variables);
    mem_free(scope);
}

static void free_function_entry(void *entry) {
    FunctionEntry *fe = (FunctionEntry*)entry;
    if (fe) {
        if (fe->description) mem_free(fe->description);
        mem_free(fe);
    }
}

static void free_consequence_handler(void *handler) {
    ConsequenceHandler *ch = (ConsequenceHandler*)handler;
    if (ch) {
        if (ch->name) mem_free(ch->name);
        mem_free(ch);
    }
}

static reasons_value_t execute_builtin(runtime_env_t *env, 
                                      const char *func_name,
                                      const reasons_value_t *args, 
                                      size_t num_args) {
    reasons_value_t result = {VALUE_NULL};
    
    // Math functions
    if (strcmp(func_name, "abs") == 0) {
        if (num_args == 1 && args[0].type == VALUE_NUMBER) {
            result.type = VALUE_NUMBER;
            result.data.number_val = fabs(args[0].data.number_val);
        } else {
            runtime_set_error(env, ERROR_ARGUMENT, "abs() requires one number argument");
        }
    }
    else if (strcmp(func_name, "sqrt") == 0) {
        if (num_args == 1 && args[0].type == VALUE_NUMBER) {
            if (args[0].data.number_val >= 0) {
                result.type = VALUE_NUMBER;
                result.data.number_val = sqrt(args[0].data.number_val);
            } else {
                runtime_set_error(env, ERROR_DOMAIN, "sqrt() argument must be non-negative");
            }
        } else {
            runtime_set_error(env, ERROR_ARGUMENT, "sqrt() requires one number argument");
        }
    }
    // String functions
    else if (strcmp(func_name, "strlen") == 0) {
        if (num_args == 1 && args[0].type == VALUE_STRING) {
            result.type = VALUE_NUMBER;
            result.data.number_val = strlen(args[0].data.string_val);
        } else {
            runtime_set_error(env, ERROR_ARGUMENT, "strlen() requires one string argument");
        }
    }
    else if (strcmp(func_name, "substr") == 0) {
        if (num_args == 3 && 
            args[0].type == VALUE_STRING &&
            args[1].type == VALUE_NUMBER &&
            args[2].type == VALUE_NUMBER) {
            
            const char *str = args[0].data.string_val;
            int start = (int)args[1].data.number_val;
            int length = (int)args[2].data.number_val;
            
            if (start < 0 || length < 0 || start + length > (int)strlen(str)) {
                runtime_set_error(env, ERROR_RANGE, "substr() index out of bounds");
            } else {
                char *sub = mem_alloc(length + 1);
                if (sub) {
                    strncpy(sub, str + start, length);
                    sub[length] = '\0';
                    result.type = VALUE_STRING;
                    result.data.string_val = sub;
                } else {
                    runtime_set_error(env, ERROR_MEMORY, "Memory allocation failed");
                }
            }
        } else {
            runtime_set_error(env, ERROR_ARGUMENT, "substr() requires (string, start, length)");
        }
    }
    // Statistical functions
    else if (strcmp(func_name, "mean") == 0) {
        if (num_args >= 1) {
            double sum = 0.0;
            for (size_t i = 0; i < num_args; i++) {
                if (args[i].type != VALUE_NUMBER) {
                    runtime_set_error(env, ERROR_TYPE, "mean() requires number arguments");
                    return result;
                }
                sum += args[i].data.number_val;
            }
            result.type = VALUE_NUMBER;
            result.data.number_val = sum / num_args;
        } else {
            runtime_set_error(env, ERROR_ARGUMENT, "mean() requires at least one argument");
        }
    }
    // Date/time functions
    else if (strcmp(func_name, "now") == 0) {
        if (num_args == 0) {
            result.type = VALUE_NUMBER;
            result.data.number_val = (double)time(NULL);
        } else {
            runtime_set_error(env, ERROR_ARGUMENT, "now() takes no arguments");
        }
    }
    // Default case
    else {
        runtime_set_error(env, ERROR_UNDEFINED, "Built-in function not implemented");
    }
    
    return result;
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

/* Environment creation/destruction */
runtime_env_t* runtime_create() {
    runtime_env_t *env = mem_alloc(sizeof(runtime_env_t));
    if (env) {
        memset(env, 0, sizeof(runtime_env_t));
        
        // Create global scope
        env->current_scope = scope_create(NULL);
        
        // Initialize collections
        env->functions = hash_create(64);
        env->call_stack = vector_create(16);
        env->consequence_handlers = vector_create(8);
        
        // Set default configuration
        env->config.golf_mode = true;
        env->config.max_recursion_depth = 100;
        env->config.enable_tracing = false;
        env->config.enable_explanations = true;
        env->config.gc_threshold = 1024;
        
        // Initialize statistics
        env->stats.variables_created = 0;
        env->stats.functions_called = 0;
        env->stats.consequences_executed = 0;
        env->stats.memory_allocated = 0;
        env->stats.start_time = clock();
        
        // Register standard library functions
        runtime_register_function(env, "abs", math_abs, "Absolute value", 1, 1);
        runtime_register_function(env, "sqrt", math_sqrt, "Square root", 1, 1);
        runtime_register_function(env, "log", math_log, "Natural logarithm", 1, 1);
        runtime_register_function(env, "pow", math_pow, "Power function", 2, 2);
        runtime_register_function(env, "strlen", string_length, "String length", 1, 1);
        runtime_register_function(env, "substr", string_substr, "Substring", 3, 3);
        runtime_register_function(env, "mean", stats_mean, "Arithmetic mean", 1, VAR_ARGS);
        runtime_register_function(env, "now", datetime_now, "Current timestamp", 0, 0);
    }
    return env;
}

void runtime_destroy(runtime_env_t *env) {
    if (!env) return;
    
    // Destroy all scopes
    Scope *scope = env->current_scope;
    while (scope) {
        Scope *parent = scope->parent;
        scope_destroy(scope);
        scope = parent;
    }
    
    // Destroy function registry
    hash_destroy_custom(env->functions, free_function_entry);
    
    // Destroy consequence handlers
    vector_destroy_custom(env->consequence_handlers, free_consequence_handler);
    
    // Destroy call stack
    vector_destroy(env->call_stack);
    
    // Free error message
    if (env->error_message) mem_free(env->error_message);
    
    mem_free(env);
}

/* Variable management */
bool runtime_set_variable(runtime_env_t *env, const char *name, reasons_value_t value) {
    if (!env || !name) return false;
    
    // Check if variable exists in current scope
    reasons_value_t *existing = hash_get(env->current_scope->variables, name);
    if (existing) {
        reasons_value_free(existing);
        *existing = reasons_value_clone(&value);
        return true;
    }
    
    // Create new variable
    reasons_value_t *copy = mem_alloc(sizeof(reasons_value_t));
    if (!copy) return false;
    
    *copy = reasons_value_clone(&value);
    const char *key = string_duplicate(name);
    if (!key) {
        mem_free(copy);
        return false;
    }
    
    if (!hash_set(env->current_scope->variables, key, copy)) {
        mem_free(copy);
        mem_free((void*)key);
        return false;
    }
    
    env->stats.variables_created++;
    return true;
}

reasons_value_t runtime_get_variable(runtime_env_t *env, const char *name) {
    reasons_value_t result = {VALUE_NULL};
    if (!env || !name) return result;
    
    // Search current and parent scopes
    Scope *scope = env->current_scope;
    while (scope) {
        reasons_value_t *value = hash_get(scope->variables, name);
        if (value) return *value;
        scope = scope->parent;
    }
    
    runtime_set_error(env, ERROR_UNDEFINED, "Variable not found");
    return result;
}

bool runtime_variable_exists(runtime_env_t *env, const char *name) {
    if (!env || !name) return false;
    
    Scope *scope = env->current_scope;
    while (scope) {
        if (hash_contains(scope->variables, name)) return true;
        scope = scope->parent;
    }
    return false;
}

/* Scope management */
void runtime_push_scope(runtime_env_t *env) {
    if (!env) return;
    
    Scope *new_scope = scope_create(env->current_scope);
    if (new_scope) {
        env->current_scope = new_scope;
    }
}

void runtime_pop_scope(runtime_env_t *env) {
    if (!env || !env->current_scope || !env->current_scope->parent) return;
    
    Scope *old_scope = env->current_scope;
    env->current_scope = old_scope->parent;
    scope_destroy(old_scope);
}

/* Function management */
bool runtime_register_function(runtime_env_t *env, const char *name, 
                              runtime_function_t function, const char *description,
                              unsigned min_args, unsigned max_args) {
    if (!env || !name || !function) return false;
    
    FunctionEntry *entry = mem_alloc(sizeof(FunctionEntry));
    if (!entry) return false;
    
    entry->function = function;
    entry->min_args = min_args;
    entry->max_args = max_args;
    entry->description = description ? string_duplicate(description) : NULL;
    
    const char *key = string_duplicate(name);
    if (!key) {
        free_function_entry(entry);
        return false;
    }
    
    if (!hash_set(env->functions, key, entry)) {
        mem_free((void*)key);
        free_function_entry(entry);
        return false;
    }
    
    return true;
}

reasons_value_t runtime_call_function(runtime_env_t *env, const char *name, 
                                     const reasons_value_t *args, size_t num_args) {
    reasons_value_t result = {VALUE_NULL};
    if (!env || !name) return result;
    
    // Check recursion depth
    if (vector_size(env->call_stack) {
        env->stats.recursion_depth = vector_size(env->call_stack);
        if (env->stats.recursion_depth > env->config.max_recursion_depth) {
            runtime_set_error(env, ERROR_RECURSION, "Maximum recursion depth exceeded");
            return result;
        }
    }
    
    // Get function entry
    FunctionEntry *entry = hash_get(env->functions, name);
    if (!entry) {
        // Check built-in functions
        return execute_builtin(env, name, args, num_args);
    }
    
    // Validate arguments
    if (num_args < entry->min_args || 
        (entry->max_args != VAR_ARGS && num_args > entry->max_args)) {
        runtime_set_error(env, ERROR_ARGUMENT, "Invalid number of arguments");
        return result;
    }
    
    // Push to call stack
    vector_push(env->call_stack, (void*)name);
    
    // Call the function
    result = entry->function(env, args, num_args);
    
    // Pop from call stack
    vector_pop(env->call_stack);
    
    // Update statistics
    env->stats.functions_called++;
    env->stats.max_recursion_depth = 
        MAX(env->stats.max_recursion_depth, env->stats.recursion_depth);
    
    return result;
}

/* Consequence handling */
void runtime_register_consequence_handler(runtime_env_t *env, consequence_type_t type,
                                         consequence_handler_t handler, const char *name) {
    if (!env || !handler) return;
    
    ConsequenceHandler *ch = mem_alloc(sizeof(ConsequenceHandler));
    if (ch) {
        ch->type = type;
        ch->handler = handler;
        ch->name = name ? string_duplicate(name) : NULL;
        vector_append(env->consequence_handlers, ch);
    }
}

consequence_result_t runtime_execute_consequence(runtime_env_t *env, AST_Node *action, 
                                                consequence_type_t type) {
    consequence_result_t result = {false, NULL, NULL};
    if (!env || !action) return result;
    
    // Find appropriate handler
    for (size_t i = 0; i < vector_size(env->consequence_handlers); i++) {
        ConsequenceHandler *ch = vector_at(env->consequence_handlers, i);
        if (ch->type == type || ch->type == CONSEQUENCE_ANY) {
            result = ch->handler(env, action);
            if (result.handled) break;
        }
    }
    
    // Update statistics
    env->stats.consequences_executed++;
    if (result.success) {
        env->stats.successful_consequences++;
    } else {
        env->stats.failed_consequences++;
    }
    
    return result;
}

/* Configuration */
void runtime_set_option(runtime_env_t *env, runtime_option_t option, const void *value) {
    if (!env || !value) return;
    
    switch (option) {
        case RUNTIME_OPTION_GOLF_MODE:
            env->config.golf_mode = *(bool*)value;
            break;
        case RUNTIME_OPTION_MAX_RECURSION:
            env->config.max_recursion_depth = *(unsigned*)value;
            break;
        case RUNTIME_OPTION_TRACING:
            env->config.enable_tracing = *(bool*)value;
            break;
        case RUNTIME_OPTION_EXPLANATIONS:
            env->config.enable_explanations = *(bool*)value;
            break;
        case RUNTIME_OPTION_GC_THRESHOLD:
            env->config.gc_threshold = *(size_t*)value;
            break;
    }
}

void* runtime_get_option(runtime_env_t *env, runtime_option_t option) {
    static bool bool_val;
    static unsigned uint_val;
    static size_t size_val;
    
    if (!env) return NULL;
    
    switch (option) {
        case RUNTIME_OPTION_GOLF_MODE:
            bool_val = env->config.golf_mode;
            return &bool_val;
        case RUNTIME_OPTION_MAX_RECURSION:
            uint_val = env->config.max_recursion_depth;
            return &uint_val;
        case RUNTIME_OPTION_TRACING:
            bool_val = env->config.enable_tracing;
            return &bool_val;
        case RUNTIME_OPTION_EXPLANATIONS:
            bool_val = env->config.enable_explanations;
            return &bool_val;
        case RUNTIME_OPTION_GC_THRESHOLD:
            size_val = env->config.gc_threshold;
            return &size_val;
        default:
            return NULL;
    }
}

/* Error handling */
void runtime_set_error(runtime_env_t *env, error_t code, const char *message) {
    if (!env) return;
    
    env->last_error = code;
    if (env->error_message) mem_free(env->error_message);
    env->error_message = message ? string_duplicate(message) : NULL;
    
    // Update statistics
    env->stats.errors_occurred++;
}

error_t runtime_last_error(runtime_env_t *env) {
    return env ? env->last_error : ERROR_NONE;
}

const char* runtime_error_message(runtime_env_t *env) {
    if (!env) return "Invalid runtime environment";
    if (env->error_message) return env->error_message;
    return error_message(env->last_error);
}

/* Statistics */
runtime_stats_t runtime_get_stats(runtime_env_t *env) {
    if (env) {
        // Calculate current memory usage
        env->stats.memory_allocated = memory_current_usage();
        
        // Calculate uptime
        clock_t now = clock();
        env->stats.uptime_seconds = (double)(now - env->stats.start_time) / CLOCKS_PER_SEC;
        
        return env->stats;
    }
    
    runtime_stats_t empty = {0};
    return empty;
}

void runtime_reset_stats(runtime_env_t *env) {
    if (env) {
        memset(&env->stats, 0, sizeof(runtime_stats_t));
        env->stats.start_time = clock();
    }
}

/* Memory management */
void runtime_gc(runtime_env_t *env) {
    if (!env) return;
    
    // Simple garbage collection - free unused scopes
    // More advanced implementation would track references
    if (env->current_scope && env->current_scope->parent) {
        // Don't collect current scope or global scope
    }
    
    // Update stats
    env->stats.gc_runs++;
    env->stats.last_gc_freed = memory_collect();
}
