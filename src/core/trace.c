/*
 * trace.c - Execution Tracing Engine for Reasons DSL
 * 
 * Provides detailed execution tracing for decision trees, including:
 * - Step-by-step execution tracking
 * - Condition evaluation recording
 * - Decision path visualization
 * - Performance metrics collection
 * - Golf mode optimizations
 * - Stack trace generation
 * - Debugging support
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include "reasons/trace.h"
#include "reasons/ast.h"
#include "reasons/types.h"
#include "utils/error.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "utils/string_utils.h"

/* Maximum trace entry depth and size limits */
#define TRACE_MAX_DEPTH 1000
#define TRACE_MAX_ENTRIES 10000
#define TRACE_MAX_MESSAGE_LEN 512
#define TRACE_TIMESTAMP_BUFFER_SIZE 32

/* Trace entry types */
typedef enum {
    TRACE_ENTER_NODE,
    TRACE_EXIT_NODE,
    TRACE_CONDITION_EVAL,
    TRACE_DECISION_BRANCH,
    TRACE_CONSEQUENCE_EXEC,
    TRACE_RULE_INVOKE,
    TRACE_VALUE_CHANGE,
    TRACE_ERROR_OCCURRED,
    TRACE_BEGIN_SECTION,
    TRACE_END_SECTION,
    TRACE_CUSTOM_MESSAGE
} trace_entry_type_t;

/* Individual trace entry */
typedef struct trace_entry {
    trace_entry_type_t type;
    char timestamp[TRACE_TIMESTAMP_BUFFER_SIZE];
    unsigned long elapsed_ns;       /* Nanoseconds since trace start */
    int depth;                      /* Nesting depth */
    ast_node_t *node;              /* Associated AST node (if any) */
    char message[TRACE_MAX_MESSAGE_LEN];
    reasons_value_t value;          /* Associated value (if any) */
    bool has_value;                 /* Whether value is valid */
    struct trace_entry *next;       /* For linked list */
} trace_entry_t;

/* Trace session structure */
struct trace {
    trace_entry_t *first_entry;     /* First entry in trace */
    trace_entry_t *last_entry;      /* Last entry for O(1) append */
    trace_entry_t *current_entry;   /* Current position for iteration */
    size_t entry_count;             /* Total number of entries */
    size_t max_entries;             /* Maximum allowed entries */
    int current_depth;              /* Current nesting depth */
    int max_depth_reached;          /* Maximum depth reached */
    bool enabled;                   /* Whether tracing is active */
    bool detailed_mode;             /* Include detailed information */
    bool timestamp_mode;            /* Include timestamps */
    struct timespec start_time;     /* Trace session start time */
    trace_stats_t stats;            /* Trace statistics */
    vector_t *node_stack;           /* Node execution stack */
    hash_table_t *node_counts;      /* Node execution counts */
    FILE *output_file;              /* Optional output file */
};

/* Forward declarations */
static trace_entry_t *trace_entry_create(trace_entry_type_t type, int depth, 
                                         ast_node_t *node, const char *message);
static void trace_entry_destroy(trace_entry_t *entry);
static void trace_add_entry(trace_t *trace, trace_entry_t *entry);
static void format_timestamp(char *buffer, size_t size, struct timespec *time);
static unsigned long calculate_elapsed_ns(const struct timespec *start, 
                                         const struct timespec *current);
static const char *trace_entry_type_name(trace_entry_type_t type);
static void trace_update_stats(trace_t *trace, trace_entry_t *entry);

/* Trace creation and destruction */
trace_t *trace_create(void)
{
    trace_t *trace = memory_allocate(sizeof(trace_t));
    if (!trace) {
        error_set(ERROR_MEMORY, "Failed to allocate trace structure");
        return NULL;
    }

    memset(trace, 0, sizeof(trace_t));
    trace->enabled = true;
    trace->detailed_mode = true;
    trace->timestamp_mode = true;
    trace->max_entries = TRACE_MAX_ENTRIES;
    trace->node_stack = vector_create(32);
    trace->node_counts = hash_create(128);
    
    /* Record start time */
    clock_gettime(CLOCK_MONOTONIC, &trace->start_time);
    
    LOG_DEBUG("Created new trace session");
    return trace;
}

void trace_destroy(trace_t *trace)
{
    if (!trace) return;
    
    /* Free all entries */
    trace_entry_t *entry = trace->first_entry;
    while (entry) {
        trace_entry_t *next = entry->next;
        trace_entry_destroy(entry);
        entry = next;
    }
    
    /* Free resources */
    vector_destroy(trace->node_stack);
    hash_destroy(trace->node_counts);
    
    /* Close output file if open */
    if (trace->output_file && trace->output_file != stdout && trace->output_file != stderr) {
        fclose(trace->output_file);
    }
    
    memory_free(trace);
    LOG_DEBUG("Destroyed trace session");
}

/* Configuration */
void trace_set_enabled(trace_t *trace, bool enabled)
{
    if (trace) trace->enabled = enabled;
}

void trace_set_detailed(trace_t *trace, bool detailed)
{
    if (trace) trace->detailed_mode = detailed;
}

void trace_set_timestamps(trace_t *trace, bool timestamps)
{
    if (trace) trace->timestamp_mode = timestamps;
}

void trace_set_max_entries(trace_t *trace, size_t max_entries)
{
    if (trace) trace->max_entries = max_entries;
}

bool trace_set_output_file(trace_t *trace, const char *filename)
{
    if (!trace || !filename) {
        error_set(ERROR_INVALID_ARGUMENT, "Invalid arguments to trace_set_output_file");
        return false;
    }
    
    /* Close existing file if open */
    if (trace->output_file && trace->output_file != stdout && trace->output_file != stderr) {
        fclose(trace->output_file);
    }
    
    /* Open new file */
    trace->output_file = fopen(filename, "w");
    if (!trace->output_file) {
        error_set(ERROR_IO, "Failed to open trace output file");
        return false;
    }
    
    return true;
}

/* Trace session management */
void trace_clear(trace_t *trace)
{
    if (!trace) return;
    
    /* Free all entries */
    trace_entry_t *entry = trace->first_entry;
    while (entry) {
        trace_entry_t *next = entry->next;
        trace_entry_destroy(entry);
        entry = next;
    }
    
    /* Reset state */
    trace->first_entry = NULL;
    trace->last_entry = NULL;
    trace->current_entry = NULL;
    trace->entry_count = 0;
    trace->current_depth = 0;
    trace->max_depth_reached = 0;
    memset(&trace->stats, 0, sizeof(trace_stats_t));
    
    /* Clear collections */
    vector_clear(trace->node_stack);
    hash_clear(trace->node_counts);
    
    /* Reset start time */
    clock_gettime(CLOCK_MONOTONIC, &trace->start_time);
    
    LOG_DEBUG("Cleared trace session");
}

void trace_begin(trace_t *trace, const char *name)
{
    if (!trace || !trace->enabled) return;
    
    char message[TRACE_MAX_MESSAGE_LEN];
    snprintf(message, sizeof(message), "Begin: %s", name ? name : "Unnamed");
    
    trace_entry_t *entry = trace_entry_create(TRACE_BEGIN_SECTION, 
                                             trace->current_depth, NULL, message);
    if (entry) {
        trace_add_entry(trace, entry);
        trace->current_depth++;
        trace->stats.sections_begun++;
    }
}

void trace_end(trace_t *trace)
{
    if (!trace || !trace->enabled) return;
    
    if (trace->current_depth > 0) {
        trace->current_depth--;
    }
    
    trace_entry_t *entry = trace_entry_create(TRACE_END_SECTION, 
                                             trace->current_depth, NULL, "End section");
    if (entry) {
        trace_add_entry(trace, entry);
        trace->stats.sections_ended++;
    }
}

/* Node execution tracing */
void trace_enter_node(trace_t *trace, ast_node_t *node)
{
    if (!trace || !trace->enabled || !node) return;
    
    /* Check depth limit */
    if (trace->current_depth >= TRACE_MAX_DEPTH) {
        trace_message(trace, "Maximum trace depth exceeded");
        return;
    }
    
    char message[TRACE_MAX_MESSAGE_LEN];
    const char *node_type = ast_node_type_name(node->type);
    
    /* Create detailed message based on node type */
    switch (node->type) {
        case AST_DECISION:
            snprintf(message, sizeof(message), "Enter Decision: \"%s\"", 
                    node->data.decision.condition);
            break;
        case AST_CONSEQUENCE:
            snprintf(message, sizeof(message), "Enter Consequence: \"%s\"", 
                    node->data.consequence.action);
            break;
        case AST_RULE:
            snprintf(message, sizeof(message), "Enter Rule: \"%s\"", 
                    node->data.rule.name);
            break;
        case AST_IDENTIFIER:
            snprintf(message, sizeof(message), "Enter Identifier: \"%s\"", 
                    node->data.identifier.name);
            break;
        default:
            snprintf(message, sizeof(message), "Enter %s", node_type);
            break;
    }
    
    trace_entry_t *entry = trace_entry_create(TRACE_ENTER_NODE, 
                                             trace->current_depth, node, message);
    if (entry) {
        trace_add_entry(trace, entry);
        trace->current_depth++;
        
        /* Update node execution count */
        uintptr_t node_key = (uintptr_t)node;
        size_t *count = hash_get(trace->node_counts, node_key);
        if (count) {
            (*count)++;
        } else {
            size_t *new_count = memory_allocate(sizeof(size_t));
            *new_count = 1;
            hash_set(trace->node_counts, node_key, new_count);
        }
        
        /* Push onto node stack */
        vector_push(trace->node_stack, node);
        
        trace->stats.nodes_entered++;
    }
}

void trace_exit_node(trace_t *trace, ast_node_t *node, const reasons_value_t *result)
{
    if (!trace || !trace->enabled || !node) return;
    
    if (trace->current_depth > 0) {
        trace->current_depth--;
    }
    
    char message[TRACE_MAX_MESSAGE_LEN];
    const char *node_type = ast_node_type_name(node->type);
    
    /* Include result value if available and detailed mode is on */
    if (result && trace->detailed_mode) {
        char value_str[128];
        reasons_value_to_string(result, value_str, sizeof(value_str));
        snprintf(message, sizeof(message), "Exit %s -> %s", node_type, value_str);
    } else {
        snprintf(message, sizeof(message), "Exit %s", node_type);
    }
    
    trace_entry_t *entry = trace_entry_create(TRACE_EXIT_NODE, 
                                             trace->current_depth, node, message);
    if (entry) {
        if (result) {
            entry->value = *result;
            entry->has_value = true;
        }
        trace_add_entry(trace, entry);
        
        /* Pop from node stack */
        if (vector_size(trace->node_stack) > 0) {
            vector_pop(trace->node_stack);
        }
        
        trace->stats.nodes_exited++;
    }
}

/* Condition and decision tracing */
void trace_condition(trace_t *trace, ast_node_t *node, const reasons_value_t *condition_value)
{
    if (!trace || !trace->enabled || !node || !condition_value) return;
    
    char message[TRACE_MAX_MESSAGE_LEN];
    char value_str[128];
    reasons_value_to_string(condition_value, value_str, sizeof(value_str));
    
    if (node->type == AST_DECISION && node->data.decision.condition) {
        snprintf(message, sizeof(message), "Condition \"%s\" evaluated to: %s", 
                node->data.decision.condition, value_str);
    } else {
        snprintf(message, sizeof(message), "Condition evaluated to: %s", value_str);
    }
    
    trace_entry_t *entry = trace_entry_create(TRACE_CONDITION_EVAL, 
                                             trace->current_depth, node, message);
    if (entry) {
        entry->value = *condition_value;
        entry->has_value = true;
        trace_add_entry(trace, entry);
        trace->stats.conditions_evaluated++;
    }
}

void trace_decision(trace_t *trace, ast_node_t *node, bool took_true_branch, ast_node_t *branch_taken)
{
    if (!trace || !trace->enabled || !node) return;
    
    char message[TRACE_MAX_MESSAGE_LEN];
    const char *branch_name = took_true_branch ? "TRUE" : "FALSE";
    
    if (branch_taken) {
        const char *branch_type = ast_node_type_name(branch_taken->type);
        snprintf(message, sizeof(message), "Decision took %s branch -> %s", 
                branch_name, branch_type);
    } else {
        snprintf(message, sizeof(message), "Decision took %s branch -> (no action)", 
                branch_name);
    }
    
    trace_entry_t *entry = trace_entry_create(TRACE_DECISION_BRANCH, 
                                             trace->current_depth, node, message);
    if (entry) {
        entry->value.type = VALUE_BOOL;
        entry->value.data.bool_val = took_true_branch;
        entry->has_value = true;
        trace_add_entry(trace, entry);
        trace->stats.decisions_made++;
    }
}

/* Consequence execution tracing */
void trace_consequence(trace_t *trace, ast_node_t *node, bool success)
{
    if (!trace || !trace->enabled || !node) return;
    
    char message[TRACE_MAX_MESSAGE_LEN];
    const char *status = success ? "SUCCESS" : "FAILED";
    
    if (node->data.consequence.action) {
        snprintf(message, sizeof(message), "Consequence \"%s\" -> %s", 
                node->data.consequence.action, status);
    } else {
        snprintf(message, sizeof(message), "Consequence execution -> %s", status);
    }
    
    trace_entry_t *entry = trace_entry_create(TRACE_CONSEQUENCE_EXEC, 
                                             trace->current_depth, node, message);
    if (entry) {
        entry->value.type = VALUE_BOOL;
        entry->value.data.bool_val = success;
        entry->has_value = true;
        trace_add_entry(trace, entry);
        
        if (success) {
            trace->stats.consequences_succeeded++;
        } else {
            trace->stats.consequences_failed++;
        }
    }
}

/* Rule execution tracing */
void trace_rule_execution(trace_t *trace, ast_node_t *node, const reasons_value_t *result)
{
    if (!trace || !trace->enabled || !node) return;
    
    char message[TRACE_MAX_MESSAGE_LEN];
    char result_str[128] = "void";
    
    if (result) {
        reasons_value_to_string(result, result_str, sizeof(result_str));
    }
    
    if (node->data.rule.name) {
        snprintf(message, sizeof(message), "Rule \"%s\" executed -> %s", 
                node->data.rule.name, result_str);
    } else {
        snprintf(message, sizeof(message), "Rule executed -> %s", result_str);
    }
    
    trace_entry_t *entry = trace_entry_create(TRACE_RULE_INVOKE, 
                                             trace->current_depth, node, message);
    if (entry) {
        if (result) {
            entry->value = *result;
            entry->has_value = true;
        }
        trace_add_entry(trace, entry);
        trace->stats.rules_executed++;
    }
}

/* Variable change tracing */
void trace_variable_change(trace_t *trace, const char *name, 
                          const reasons_value_t *old_value, 
                          const reasons_value_t *new_value)
{
    if (!trace || !trace->enabled || !name) return;
    
    char message[TRACE_MAX_MESSAGE_LEN];
    char old_str[128] = "undefined";
    char new_str[128] = "undefined";
    
    if (old_value) {
        reasons_value_to_string(old_value, old_str, sizeof(old_str));
    }
    if (new_value) {
        reasons_value_to_string(new_value, new_str, sizeof(new_str));
    }
    
    snprintf(message, sizeof(message), "Variable \"%s\": %s -> %s", 
            name, old_str, new_str);
    
    trace_entry_t *entry = trace_entry_create(TRACE_VALUE_CHANGE, 
                                             trace->current_depth, NULL, message);
    if (entry) {
        if (new_value) {
            entry->value = *new_value;
            entry->has_value = true;
        }
        trace_add_entry(trace, entry);
        trace->stats.variables_changed++;
    }
}

/* Error tracing */
void trace_error(trace_t *trace, const char *error_message)
{
    if (!trace || !trace->enabled) return;
    
    char message[TRACE_MAX_MESSAGE_LEN];
    snprintf(message, sizeof(message), "ERROR: %s", 
            error_message ? error_message : "Unknown error");
    
    trace_entry_t *entry = trace_entry_create(TRACE_ERROR_OCCURRED, 
                                             trace->current_depth, NULL, message);
    if (entry) {
        trace_add_entry(trace, entry);
        trace->stats.errors_occurred++;
    }
}

/* Custom message tracing */
void trace_message(trace_t *trace, const char *format, ...)
{
    if (!trace || !trace->enabled || !format) return;
    
    char message[TRACE_MAX_MESSAGE_LEN];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    trace_entry_t *entry = trace_entry_create(TRACE_CUSTOM_MESSAGE, 
                                             trace->current_depth, NULL, message);
    if (entry) {
        trace_add_entry(trace, entry);
        trace->stats.custom_messages++;
    }
}

/* Trace output and formatting */
void trace_print(const trace_t *trace, FILE *fp)
{
    if (!trace) return;
    if (!fp) fp = stdout;
    
    fprintf(fp, "=== EXECUTION TRACE ===\n");
    fprintf(fp, "Entries: %zu, Max Depth: %d\n\n", 
            trace->entry_count, trace->max_depth_reached);
    
    const trace_entry_t *entry = trace->first_entry;
    while (entry) {
        /* Print indentation based on depth */
        for (int i = 0; i < entry->depth; i++) {
            fprintf(fp, "  ");
        }
        
        /* Print timestamp if enabled */
        if (trace->timestamp_mode) {
            fprintf(fp, "[%s] ", entry->timestamp);
        }
        
        /* Print entry type indicator */
        switch (entry->type) {
            case TRACE_ENTER_NODE:       fprintf(fp, "-> "); break;
            case TRACE_EXIT_NODE:        fprintf(fp, "<- "); break;
            case TRACE_CONDITION_EVAL:   fprintf(fp, "?  "); break;
            case TRACE_DECISION_BRANCH:  fprintf(fp, "!  "); break;
            case TRACE_CONSEQUENCE_EXEC: fprintf(fp, "#  "); break;
            case TRACE_RULE_INVOKE:      fprintf(fp, "@  "); break;
            case TRACE_VALUE_CHANGE:     fprintf(fp, "=  "); break;
            case TRACE_ERROR_OCCURRED:   fprintf(fp, "X  "); break;
            case TRACE_BEGIN_SECTION:    fprintf(fp, "{  "); break;
            case TRACE_END_SECTION:      fprintf(fp, "}  "); break;
            case TRACE_CUSTOM_MESSAGE:   fprintf(fp, "*  "); break;
        }
        
        /* Print message */
        fprintf(fp, "%s", entry->message);
        
        /* Print value if available and detailed mode */
        if (entry->has_value && trace->detailed_mode) {
            char value_str[128];
            reasons_value_to_string(&entry->value, value_str, sizeof(value_str));
            fprintf(fp, " [value: %s]", value_str);
        }
        
        /* Print elapsed time if available */
        if (trace->timestamp_mode && entry->elapsed_ns > 0) {
            double elapsed_ms = entry->elapsed_ns / 1000000.0;
            fprintf(fp, " (+%.3fms)", elapsed_ms);
        }
        
        fprintf(fp, "\n");
        entry = entry->next;
    }
    
    fprintf(fp, "\n=== TRACE STATISTICS ===\n");
    trace_print_stats(trace, fp);
}

void trace_print_compact(const trace_t *trace, FILE *fp)
{
    if (!trace) return;
    if (!fp) fp = stdout;
    
    fprintf(fp, "TRACE: %zu entries, depth %d\n", 
            trace->entry_count, trace->max_depth_reached);
    
    const trace_entry_t *entry = trace->first_entry;
    while (entry) {
        switch (entry->type) {
            case TRACE_ENTER_NODE:
            case TRACE_EXIT_NODE:
            case TRACE_DECISION_BRANCH:
                fprintf(fp, "%*s%s\n", entry->depth * 2, "", entry->message);
                break;
            case TRACE_ERROR_OCCURRED:
                fprintf(fp, "%*sERROR: %s\n", entry->depth * 2, "", entry->message);
                break;
            default:
                /* Skip other types in compact mode */
                break;
        }
        entry = entry->next;
    }
}

void trace_print_stats(const trace_t *trace, FILE *fp)
{
    if (!trace) return;
    if (!fp) fp = stdout;
    
    const trace_stats_t *stats = &trace->stats;
    
    fprintf(fp, "Nodes entered: %zu\n", stats->nodes_entered);
    fprintf(fp, "Nodes exited: %zu\n", stats->nodes_exited);
    fprintf(fp, "Conditions evaluated: %zu\n", stats->conditions_evaluated);
    fprintf(fp, "Decisions made: %zu\n", stats->decisions_made);
    fprintf(fp, "Consequences succeeded: %zu\n", stats->consequences_succeeded);
    fprintf(fp, "Consequences failed: %zu\n", stats->consequences_failed);
    fprintf(fp, "Rules executed: %zu\n", stats->rules_executed);
    fprintf(fp, "Variables changed: %zu\n", stats->variables_changed);
    fprintf(fp, "Errors occurred: %zu\n", stats->errors_occurred);
    fprintf(fp, "Custom messages: %zu\n", stats->custom_messages);
    fprintf(fp, "Sections begun: %zu\n", stats->sections_begun);
    fprintf(fp, "Sections ended: %zu\n", stats->sections_ended);
    fprintf(fp, "Maximum depth reached: %d\n", trace->max_depth_reached);
}

/* Trace iteration */
void trace_rewind(trace_t *trace)
{
    if (trace) {
        trace->current_entry = trace->first_entry;
    }
}

const trace_entry_t *trace_next(trace_t *trace)
{
    if (!trace || !trace->current_entry) return NULL;
    
    const trace_entry_t *entry = trace->current_entry;
    trace->current_entry = entry->next;
    return entry;
}

bool trace_has_more(const trace_t *trace)
{
    return trace && trace->current_entry != NULL;
}

/* Stack trace generation */
char *trace_get_stack_trace(const trace_t *trace)
{
    if (!trace || vector_size(trace->node_stack) == 0) {
        return string_duplicate("(empty stack)");
    }
    
    size_t stack_size = vector_size(trace->node_stack);
    size_t buffer_size = stack_size * 256;  /* Estimate */
    char *buffer = memory_allocate(buffer_size);
    if (!buffer) return NULL;
    
    buffer[0] = '\0';
    
    for (size_t i = 0; i < stack_size; i++) {
        ast_node_t *node = vector_get(trace->node_stack, i);
        if (node) {
            char line[256];
            const char *type_name = ast_node_type_name(node->type);
            
            switch (node->type) {
                case AST_DECISION:
                    snprintf(line, sizeof(line), "  %zu: Decision \"%s\"\n", 
                            i, node->data.decision.condition);
                    break;
                case AST_RULE:
                    snprintf(line, sizeof(line), "  %zu: Rule \"%s\"\n", 
                            i, node->data.rule.name);
                    break;
                case AST_CONSEQUENCE:
                    snprintf(line, sizeof(line), "  %zu: Consequence \"%s\"\n", 
                            i, node->data.consequence.action);
                    break;
                default:
                    snprintf(line, sizeof(line), "  %zu: %s\n", i, type_name);
                    break;
            }
            
            strncat(buffer, line, buffer_size - strlen(buffer) - 1);
        }
    }
    
    return buffer;
}

/* Query functions */
size_t trace_get_entry_count(const trace_t *trace)
{
    return trace ? trace->entry_count : 0;
}

int trace_get_max_depth(const trace_t *trace)
{
    return trace ? trace->max_depth_reached : 0;
}

const trace_stats_t *trace_get_stats(const trace_t *trace)
{
    return trace ? &trace->stats : NULL;
}

bool trace_is_enabled(const trace_t *trace)
{
    return trace ? trace->enabled : false;
}

size_t trace_get_node_execution_count(const trace_t *trace, ast_node_t *node)
{
    if (!trace || !node) return 0;
    
    uintptr_t node_key = (uintptr_t)node;
    size_t *count = hash_get(trace->node_counts, node_key);
    return count ? *count : 0;
}

/* Export functions */
bool trace_export_json(const trace_t *trace, const char *filename)
{
    if (!trace || !filename) {
        error_set(ERROR_INVALID_ARGUMENT, "Invalid arguments to trace_export_json");
        return false;
    }
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        error_set(ERROR_IO, "Failed to open JSON export file");
        return false;
    }
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"trace\": {\n");
    fprintf(fp, "    \"entry_count\": %zu,\n", trace->entry_count);
    fprintf(fp, "    \"max_depth\": %d,\n", trace->max_depth_reached);
    fprintf(fp, "    \"entries\": [\n");
    
    const trace_entry_t *entry = trace->first_entry;
    bool first = true;
    
    while (entry) {
        if (!first) fprintf(fp, ",\n");
        first = false;
        
        fprintf(fp, "      {\n");
        fprintf(fp, "        \"type\": \"%s\",\n", trace_entry_type_name(entry->type));
        fprintf(fp, "        \"depth\": %d,\n", entry->depth);
        fprintf(fp, "        \"timestamp\": \"%s\",\n", entry->timestamp);
        fprintf(fp, "        \"elapsed_ns\": %lu,\n", entry->elapsed_ns);
        fprintf(fp, "        \"message\": \"%s\"", entry->message);
        
        if (entry->has_value) {
            char value_str[256];
            reasons_value_to_string(&entry->value, value_str, sizeof(value_str));
            fprintf(fp, ",\n        \"value\": \"%s\"", value_str);
        }
        
        fprintf(fp, "\n      }");
        entry = entry->next;
    }
    
    fprintf(fp, "\n    ],\n");
    fprintf(fp, "    \"stats\": {\n");
    fprintf(fp, "      \"nodes_entered\": %zu,\n", trace->stats.nodes_entered);
    fprintf(fp, "      \"nodes_exited\": %zu,\n", trace->stats.nodes_exited);
    fprintf(fp, "      \"conditions_evaluated\": %zu,\n", trace->stats.conditions_evaluated);
    fprintf(fp, "      \"decisions_made\": %zu,\n", trace->stats.decisions_made);
    fprintf(fp, "      \"consequences_succeeded\": %zu,\n", trace->stats.consequences_succeeded);
    fprintf(fp, "      \"consequences_failed\": %zu,\n", trace->stats.consequences_failed);
    fprintf(fp, "      \"rules_executed\": %zu,\n", trace->stats.rules_executed);
    fprintf(fp, "      \"variables_changed\": %zu,\n", trace->stats.variables_changed);
    fprintf(fp, "      \"errors_occurred\": %zu\n", trace->stats.errors_occurred);
    fprintf(fp, "    }\n");
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    return true;
}

bool trace_export_csv(const trace_t *trace, const char *filename)
{
    if (!trace || !filename) {
        error_set(ERROR_INVALID_ARGUMENT, "Invalid arguments to trace_export_csv");
        return false;
    }
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        error_set(ERROR_IO, "Failed to open CSV export file");
        return false;
    }
    
    /* Write CSV header */
    fprintf(fp, "Type,Depth,Timestamp,ElapsedNS,Message,Value\n");
    
    const trace_entry_t *entry = trace->first_entry;
    while (entry) {
        fprintf(fp, "\"%s\",%d,\"%s\",%lu,\"%s\"",
                trace_entry_type_name(entry->type),
                entry->depth,
                entry->timestamp,
                entry->elapsed_ns,
                entry->message);
        
        if (entry->has_value) {
            char value_str[256];
            reasons_value_to_string(&entry->value, value_str, sizeof(value_str));
            fprintf(fp, ",\"%s\"", value_str);
        } else {
            fprintf(fp, ",");
        }
        
        fprintf(fp, "\n");
        entry = entry->next;
    }
    
    fclose(fp);
    return true;
}

/* Internal helper functions */
static trace_entry_t *trace_entry_create(trace_entry_type_t type, int depth, 
                                         ast_node_t *node, const char *message)
{
    trace_entry_t *entry = memory_allocate(sizeof(trace_entry_t));
    if (!entry) {
        error_set(ERROR_MEMORY, "Failed to allocate trace entry");
        return NULL;
    }
    
    memset(entry, 0, sizeof(trace_entry_t));
    entry->type = type;
    entry->depth = depth;
    entry->node = node;
    entry->has_value = false;
    
    /* Copy message */
    if (message) {
        strncpy(entry->message, message, sizeof(entry->message) - 1);
        entry->message[sizeof(entry->message) - 1] = '\0';
    }
    
    /* Generate timestamp */
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    format_timestamp(entry->timestamp, sizeof(entry->timestamp), &current_time);
    
    return entry;
}

static void trace_entry_destroy(trace_entry_t *entry)
{
    if (!entry) return;
    
    /* Free any string values */
    if (entry->has_value && entry->value.type == VALUE_STRING) {
        memory_free(entry->value.data.string_val);
    }
    
    memory_free(entry);
}

static void trace_add_entry(trace_t *trace, trace_entry_t *entry)
{
    if (!trace || !entry) return;
    
    /* Check entry limit */
    if (trace->entry_count >= trace->max_entries) {
        /* Remove oldest entry to make room */
        if (trace->first_entry) {
            trace_entry_t *old_first = trace->first_entry;
            trace->first_entry = old_first->next;
            if (trace->first_entry == NULL) {
                trace->last_entry = NULL;
            }
            trace_entry_destroy(old_first);
            trace->entry_count--;
        }
    }
    
    /* Calculate elapsed time */
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    entry->elapsed_ns = calculate_elapsed_ns(&trace->start_time, &current_time);
    
    /* Add to list */
    if (!trace->first_entry) {
        trace->first_entry = entry;
        trace->last_entry = entry;
    } else {
        trace->last_entry->next = entry;
        trace->last_entry = entry;
    }
    
    trace->entry_count++;
    
    /* Update max depth */
    if (entry->depth > trace->max_depth_reached) {
        trace->max_depth_reached = entry->depth;
    }
    
    /* Update statistics */
    trace_update_stats(trace, entry);
    
    /* Write to output file if configured */
    if (trace->output_file) {
        /* Print indentation */
        for (int i = 0; i < entry->depth; i++) {
            fprintf(trace->output_file, "  ");
        }
        
        fprintf(trace->output_file, "[%s] %s: %s\n",
                entry->timestamp,
                trace_entry_type_name(entry->type),
                entry->message);
        
        fflush(trace->output_file);
    }
}

static void format_timestamp(char *buffer, size_t size, struct timespec *time)
{
    time_t sec = time->tv_sec;
    struct tm *tm_info = localtime(&sec);
    
    snprintf(buffer, size, "%02d:%02d:%02d.%03ld",
            tm_info->tm_hour,
            tm_info->tm_min,
            tm_info->tm_sec,
            time->tv_nsec / 1000000);
}

static unsigned long calculate_elapsed_ns(const struct timespec *start, 
                                         const struct timespec *current)
{
    unsigned long start_ns = start->tv_sec * 1000000000UL + start->tv_nsec;
    unsigned long current_ns = current->tv_sec * 1000000000UL + current->tv_nsec;
    return current_ns - start_ns;
}

static const char *trace_entry_type_name(trace_entry_type_t type)
{
    switch (type) {
        case TRACE_ENTER_NODE:       return "ENTER_NODE";
        case TRACE_EXIT_NODE:        return "EXIT_NODE";
        case TRACE_CONDITION_EVAL:   return "CONDITION_EVAL";
        case TRACE_DECISION_BRANCH:  return "DECISION_BRANCH";
        case TRACE_CONSEQUENCE_EXEC: return "CONSEQUENCE_EXEC";
        case TRACE_RULE_INVOKE:      return "RULE_INVOKE";
        case TRACE_VALUE_CHANGE:     return "VALUE_CHANGE";
        case TRACE_ERROR_OCCURRED:   return "ERROR_OCCURRED";
        case TRACE_BEGIN_SECTION:    return "BEGIN_SECTION";
        case TRACE_END_SECTION:      return "END_SECTION";
        case TRACE_CUSTOM_MESSAGE:   return "CUSTOM_MESSAGE";
        default:                     return "UNKNOWN";
    }
}

static void trace_update_stats(trace_t *trace, trace_entry_t *entry)
{
    switch (entry->type) {
        case TRACE_ENTER_NODE:
            trace->stats.nodes_entered++;
            break;
        case TRACE_EXIT_NODE:
            trace->stats.nodes_exited++;
            break;
        case TRACE_CONDITION_EVAL:
            trace->stats.conditions_evaluated++;
            break;
        case TRACE_DECISION_BRANCH:
            trace->stats.decisions_made++;
            break;
        case TRACE_CONSEQUENCE_EXEC:
            if (entry->has_value && entry->value.type == VALUE_BOOL) {
                if (entry->value.data.bool_val) {
                    trace->stats.consequences_succeeded++;
                } else {
                    trace->stats.consequences_failed++;
                }
            }
            break;
        case TRACE_RULE_INVOKE:
            trace->stats.rules_executed++;
            break;
        case TRACE_VALUE_CHANGE:
            trace->stats.variables_changed++;
            break;
        case TRACE_ERROR_OCCURRED:
            trace->stats.errors_occurred++;
            break;
        case TRACE_BEGIN_SECTION:
            trace->stats.sections_begun++;
            break;
        case TRACE_END_SECTION:
            trace->stats.sections_ended++;
            break;
        case TRACE_CUSTOM_MESSAGE:
            trace->stats.custom_messages++;
            break;
        default:
            break;
    }
}

/* Golf mode optimizations */
void trace_set_golf_mode(trace_t *trace, bool golf_mode)
{
    if (!trace) return;
    
    if (golf_mode) {
        /* Disable detailed tracing for better performance */
        trace->detailed_mode = false;
        trace->timestamp_mode = false;
        trace->max_entries = 1000;  /* Reduced limit */
    } else {
        /* Enable full tracing */
        trace->detailed_mode = true;
        trace->timestamp_mode = true;
        trace->max_entries = TRACE_MAX_ENTRIES;
    }
}

/* Path analysis */
char *trace_get_decision_path(const trace_t *trace)
{
    if (!trace) return NULL;
    
    vector_t *path = vector_create(64);
    if (!path) return NULL;
    
    const trace_entry_t *entry = trace->first_entry;
    while (entry) {
        if (entry->type == TRACE_DECISION_BRANCH && entry->has_value) {
            char *branch_info = memory_allocate(64);
            if (branch_info) {
                snprintf(branch_info, 64, "%s", 
                        entry->value.data.bool_val ? "TRUE" : "FALSE");
                vector_push(path, branch_info);
            }
        }
        entry = entry->next;
    }
    
    /* Build path string */
    size_t path_size = vector_size(path);
    if (path_size == 0) {
        vector_destroy(path);
        return string_duplicate("(no decisions)");
    }
    
    size_t buffer_size = path_size * 16;  /* Estimate */
    char *buffer = memory_allocate(buffer_size);
    if (!buffer) {
        vector_destroy(path);
        return NULL;
    }
    
    buffer[0] = '\0';
    for (size_t i = 0; i < path_size; i++) {
        char *branch = vector_get(path, i);
        if (i > 0) {
            strncat(buffer, " -> ", buffer_size - strlen(buffer) - 1);
        }
        strncat(buffer, branch, buffer_size - strlen(buffer) - 1);
        memory_free(branch);
    }
    
    vector_destroy(path);
    return buffer;
}

/* Performance analysis */
double trace_get_total_execution_time_ms(const trace_t *trace)
{
    if (!trace || !trace->last_entry) return 0.0;
    
    return trace->last_entry->elapsed_ns / 1000000.0;
}

double trace_get_average_node_time_ms(const trace_t *trace)
{
    if (!trace || trace->stats.nodes_entered == 0) return 0.0;
    
    double total_time = trace_get_total_execution_time_ms(trace);
    return total_time / trace->stats.nodes_entered;
}

/* Memory usage estimation */
size_t trace_get_memory_usage(const trace_t *trace)
{
    if (!trace) return 0;
    
    size_t base_size = sizeof(trace_t);
    size_t entries_size = trace->entry_count * sizeof(trace_entry_t);
    size_t stack_size = vector_capacity(trace->node_stack) * sizeof(void*);
    size_t counts_size = hash_capacity(trace->node_counts) * sizeof(size_t);
    
    return base_size + entries_size + stack_size + counts_size;
}

/* Trace filtering */
trace_t *trace_filter_by_type(const trace_t *source, trace_entry_type_t type)
{
    if (!source) return NULL;
    
    trace_t *filtered = trace_create();
    if (!filtered) return NULL;
    
    const trace_entry_t *entry = source->first_entry;
    while (entry) {
        if (entry->type == type) {
            trace_entry_t *new_entry = trace_entry_create(entry->type, entry->depth, 
                                                         entry->node, entry->message);
            if (new_entry) {
                if (entry->has_value) {
                    new_entry->value = entry->value;
                    new_entry->has_value = true;
                }
                trace_add_entry(filtered, new_entry);
            }
        }
        entry = entry->next;
    }
    
    return filtered;
}

trace_t *trace_filter_by_depth(const trace_t *source, int min_depth, int max_depth)
{
    if (!source) return NULL;
    
    trace_t *filtered = trace_create();
    if (!filtered) return NULL;
    
    const trace_entry_t *entry = source->first_entry;
    while (entry) {
        if (entry->depth >= min_depth && entry->depth <= max_depth) {
            trace_entry_t *new_entry = trace_entry_create(entry->type, entry->depth, 
                                                         entry->node, entry->message);
            if (new_entry) {
                if (entry->has_value) {
                    new_entry->value = entry->value;
                    new_entry->has_value = true;
                }
                trace_add_entry(filtered, new_entry);
            }
        }
        entry = entry->next;
    }
    
    return filtered;
}

/* Trace comparison for testing */
bool trace_compare(const trace_t *a, const trace_t *b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    
    if (a->entry_count != b->entry_count) return false;
    if (a->max_depth_reached != b->max_depth_reached) return false;
    
    const trace_entry_t *entry_a = a->first_entry;
    const trace_entry_t *entry_b = b->first_entry;
    
    while (entry_a && entry_b) {
        if (entry_a->type != entry_b->type ||
            entry_a->depth != entry_b->depth ||
            strcmp(entry_a->message, entry_b->message) != 0) {
            return false;
        }
        
        if (entry_a->has_value != entry_b->has_value) return false;
        if (entry_a->has_value && !reasons_value_equals(&entry_a->value, &entry_b->value)) {
            return false;
        }
        
        entry_a = entry_a->next;
        entry_b = entry_b->next;
    }
    
    return entry_a == NULL && entry_b == NULL;
}
