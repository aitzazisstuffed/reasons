#ifndef REASONS_TRACE_H
#define REASONS_TRACE_H

#include "reasons/ast.h"
#include "reasons/types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Trace limits */
#define TRACE_MAX_DEPTH 1000
#define TRACE_MAX_ENTRIES 10000
#define TRACE_MAX_MESSAGE_LEN 512

/* Trace statistics structure */
typedef struct {
    size_t nodes_entered;             /* Number of node entry events */
    size_t nodes_exited;              /* Number of node exit events */
    size_t conditions_evaluated;      /* Condition evaluations recorded */
    size_t decisions_made;            /* Decision branches taken */
    size_t consequences_succeeded;    /* Successful consequences */
    size_t consequences_failed;       /* Failed consequences */
    size_t rules_executed;            /* Rules executed */
    size_t variables_changed;         /* Variable changes tracked */
    size_t errors_occurred;           /* Errors recorded */
    size_t custom_messages;           /* Custom trace messages */
    size_t sections_begun;            /* Sections started */
    size_t sections_ended;            /* Sections ended */
} trace_stats_t;

/* Opaque trace structure */
typedef struct trace trace_t;

/* Creation and destruction */
trace_t *trace_create(void);
void trace_destroy(trace_t *trace);

/* Configuration */
void trace_set_enabled(trace_t *trace, bool enabled);
void trace_set_detailed(trace_t *trace, bool detailed);
void trace_set_timestamps(trace_t *trace, bool timestamps);
void trace_set_max_entries(trace_t *trace, size_t max_entries);
bool trace_set_output_file(trace_t *trace, const char *filename);
void trace_set_golf_mode(trace_t *trace, bool golf_mode);

/* Session management */
void trace_clear(trace_t *trace);
void trace_begin(trace_t *trace, const char *name);
void trace_end(trace_t *trace);

/* Event tracing */
void trace_enter_node(trace_t *trace, ast_node_t *node);
void trace_exit_node(trace_t *trace, ast_node_t *node, const reasons_value_t *result);
void trace_condition(trace_t *trace, ast_node_t *node, const reasons_value_t *condition_value);
void trace_decision(trace_t *trace, ast_node_t *node, bool took_true_branch, ast_node_t *branch_taken);
void trace_consequence(trace_t *trace, ast_node_t *node, bool success);
void trace_rule_execution(trace_t *trace, ast_node_t *node, const reasons_value_t *result);
void trace_variable_change(trace_t *trace, const char *name, 
                          const reasons_value_t *old_value, 
                          const reasons_value_t *new_value);
void trace_error(trace_t *trace, const char *error_message);
void trace_message(trace_t *trace, const char *format, ...);

/* Output and formatting */
void trace_print(const trace_t *trace, FILE *fp);
void trace_print_compact(const trace_t *trace, FILE *fp);
void trace_print_stats(const trace_t *trace, FILE *fp);

/* Trace iteration */
void trace_rewind(trace_t *trace);
const void *trace_next(trace_t *trace); /* Opaque trace entry */
bool trace_has_more(const trace_t *trace);

/* Query functions */
size_t trace_get_entry_count(const trace_t *trace);
int trace_get_max_depth(const trace_t *trace);
const trace_stats_t *trace_get_stats(const trace_t *trace);
bool trace_is_enabled(const trace_t *trace);
size_t trace_get_node_execution_count(const trace_t *trace, ast_node_t *node);
char *trace_get_stack_trace(const trace_t *trace);
char *trace_get_decision_path(const trace_t *trace);

/* Performance analysis */
double trace_get_total_execution_time_ms(const trace_t *trace);
double trace_get_average_node_time_ms(const trace_t *trace);
size_t trace_get_memory_usage(const trace_t *trace);

/* Export functions */
bool trace_export_json(const trace_t *trace, const char *filename);
bool trace_export_csv(const trace_t *trace, const char *filename);

/* Filtering and comparison */
trace_t *trace_filter_by_type(const trace_t *source, int type);
trace_t *trace_filter_by_depth(const trace_t *source, int min_depth, int max_depth);
bool trace_compare(const trace_t *a, const trace_t *b);

#endif /* REASONS_TRACE_H */
