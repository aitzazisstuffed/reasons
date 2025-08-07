#ifndef REASONS_RUNTIME_H
#define REASONS_RUNTIME_H

#include "reasons/ast.h"
#include "reasons/eval.h"
#include "utils/collections.h"
#include "utils/error.h"
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef enum {
    CONSEQUENCE_ANY,
    CONSEQUENCE_UPDATE,
    CONSEQUENCE_NOTIFY,
    CONSEQUENCE_LOG,
    CONSEQUENCE_CALCULATE
} consequence_type_t;

typedef struct runtime_env runtime_env_t;

typedef reasons_value_t (*runtime_function_t)(runtime_env_t*, const reasons_value_t*, size_t);
typedef struct {
    bool handled;
    bool success;
    reasons_value_t *value;
    char *message;
} consequence_result_t;
typedef consequence_result_t (*consequence_handler_t)(runtime_env_t*, AST_Node*);

typedef struct {
    consequence_type_t type;
    consequence_handler_t handler;
    char *name;
} ConsequenceHandler;

typedef struct {
    bool golf_mode;
    unsigned max_recursion_depth;
    bool enable_tracing;
    bool enable_explanations;
    size_t gc_threshold;
} config_t;

typedef struct {
    unsigned variables_created;
    unsigned functions_called;
    unsigned consequences_executed;
    unsigned successful_consequences;
    unsigned failed_consequences;
    unsigned recursion_depth;
    unsigned max_recursion_depth;
    unsigned errors_occurred;
    size_t memory_allocated;
    unsigned gc_runs;
    size_t last_gc_freed;
    double uptime_seconds;
    clock_t start_time;
} runtime_stats_t;

#define VAR_ARGS 0xFFFF

runtime_env_t* runtime_create(void);
void runtime_destroy(runtime_env_t *env);

bool runtime_set_variable(runtime_env_t *env, const char *name, reasons_value_t value);
reasons_value_t runtime_get_variable(runtime_env_t *env, const char *name);
bool runtime_variable_exists(runtime_env_t *env, const char *name);

void runtime_push_scope(runtime_env_t *env);
void runtime_pop_scope(runtime_env_t *env);

bool runtime_register_function(runtime_env_t *env, const char *name, 
                              runtime_function_t function, const char *description,
                              unsigned min_args, unsigned max_args);
reasons_value_t runtime_call_function(runtime_env_t *env, const char *name, 
                                     const reasons_value_t *args, size_t num_args);

void runtime_register_consequence_handler(runtime_env_t *env, consequence_type_t type,
                                         consequence_handler_t handler, const char *name);
consequence_result_t runtime_execute_consequence(runtime_env_t *env, AST_Node *action, 
                                                consequence_type_t type);

typedef enum {
    RUNTIME_OPTION_GOLF_MODE,
    RUNTIME_OPTION_MAX_RECURSION,
    RUNTIME_OPTION_TRACING,
    RUNTIME_OPTION_EXPLANATIONS,
    RUNTIME_OPTION_GC_THRESHOLD
} runtime_option_t;

void runtime_set_option(runtime_env_t *env, runtime_option_t option, const void *value);
void* runtime_get_option(runtime_env_t *env, runtime_option_t option);

void runtime_set_error(runtime_env_t *env, error_t code, const char *message);
error_t runtime_last_error(runtime_env_t *env);
const char* runtime_error_message(runtime_env_t *env);

runtime_stats_t runtime_get_stats(runtime_env_t *env);
void runtime_reset_stats(runtime_env_t *env);

void runtime_gc(runtime_env_t *env);

#endif
