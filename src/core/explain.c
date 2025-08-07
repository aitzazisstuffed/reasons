/*
 * explain.c - Explanation Engine for Reasons DSL
 * 
 * Generates human-readable "why" and "why not" explanations for decision outcomes
 * by analyzing execution traces. Integrates with the tracing system to provide:
 * - Decision path explanations
 * - Condition evaluation reasoning
 * - Consequence justification
 * - Rule activation analysis
 * - Alternative path suggestions
 * - Golf mode optimizations
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "reasons/explain.h"
#include "reasons/trace.h"
#include "reasons/ast.h"
#include "reasons/types.h"
#include "utils/error.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include "utils/collections.h"

/* Explanation context structure */
struct explain_engine {
    const trace_t *trace;           /* Execution trace to analyze */
    const ast_node_t *root;         /* Root AST node */
    char *output;                   /* Generated explanation text */
    size_t output_size;             /* Current output buffer size */
    size_t output_len;              /* Current output length */
    explain_mode_t mode;            /* Current explanation mode */
    const ast_node_t *target_node;  /* Node being explained (for focused mode) */
    bool golf_mode;                 /* Enable golf optimizations */
    vector_t *decision_path;        /* Path of decisions made */
    hash_table_t *visited_nodes;    /* Nodes already explained */
    explain_stats_t stats;          /* Explanation statistics */
};

/* Forward declarations */
static void explain_generate_why(explain_engine_t *engine);
static void explain_generate_why_not(explain_engine_t *engine);
static void explain_generate_full(explain_engine_t *engine);
static void explain_append(explain_engine_t *engine, const char *format, ...);
static void explain_decision_path(explain_engine_t *engine);
static void explain_condition_evaluations(explain_engine_t *engine);
static void explain_consequence_executions(explain_engine_t *engine);
static void explain_rule_activations(explain_engine_t *engine);
static void explain_alternative_paths(explain_engine_t *engine);
static void explain_errors(explain_engine_t *engine);
static const trace_entry_t *find_first_entry(explain_engine_t *engine, 
                                            trace_entry_type_t type, 
                                            const ast_node_t *node);
static const trace_entry_t *find_decision_entry(explain_engine_t *engine, 
                                               const ast_node_t *decision_node);
static bool was_condition_true(explain_engine_t *engine, 
                              const ast_node_t *decision_node);
static bool was_consequence_executed(explain_engine_t *engine, 
                                    const ast_node_t *consequence_node);
static bool was_rule_executed(explain_engine_t *engine, 
                             const ast_node_t *rule_node);
static const char *get_node_description(const ast_node_t *node);

/* Explanation engine creation/destruction */
explain_engine_t *explain_create(void)
{
    explain_engine_t *engine = memory_allocate(sizeof(explain_engine_t));
    if (!engine) {
        error_set(ERROR_MEMORY, "Failed to allocate explanation engine");
        return NULL;
    }

    memset(engine, 0, sizeof(explain_engine_t));
    engine->mode = EXPLAIN_WHY;
    engine->output = NULL;
    engine->output_size = 0;
    engine->output_len = 0;
    engine->golf_mode = true;
    engine->decision_path = vector_create(16);
    engine->visited_nodes = hash_create(128);
    
    LOG_DEBUG("Created explanation engine");
    return engine;
}

void explain_destroy(explain_engine_t *engine)
{
    if (!engine) return;
    
    memory_free(engine->output);
    vector_destroy(engine->decision_path);
    hash_destroy(engine->visited_nodes);
    memory_free(engine);
    
    LOG_DEBUG("Destroyed explanation engine");
}

/* Configuration */
void explain_set_mode(explain_engine_t *engine, explain_mode_t mode)
{
    if (engine) engine->mode = mode;
}

void explain_set_golf_mode(explain_engine_t *engine, bool golf_mode)
{
    if (engine) engine->golf_mode = golf_mode;
}

void explain_set_target(explain_engine_t *engine, const ast_node_t *target_node)
{
    if (engine) engine->target_node = target_node;
}

/* Explanation generation */
void explain_generate(explain_engine_t *engine, const ast_node_t *root, const trace_t *trace)
{
    if (!engine || !trace) return;
    
    /* Reset state */
    engine->root = root;
    engine->trace = trace;
    engine->output_len = 0;
    memset(&engine->stats, 0, sizeof(explain_stats_t));
    vector_clear(engine->decision_path);
    hash_clear(engine->visited_nodes);
    
    /* Generate explanation based on mode */
    switch (engine->mode) {
        case EXPLAIN_WHY:
            explain_generate_why(engine);
            break;
        case EXPLAIN_WHY_NOT:
            explain_generate_why_not(engine);
            break;
        case EXPLAIN_FULL:
            explain_generate_full(engine);
            break;
        default:
            explain_append(engine, "Invalid explanation mode");
            break;
    }
    
    /* Add final summary */
    if (engine->stats.conditions_explained > 0 || 
        engine->stats.consequences_explained > 0 ||
        engine->stats.rules_explained > 0) {
        explain_append(engine, "\n--- Explanation Summary ---");
        explain_append(engine, "Decision path length: %zu", vector_size(engine->decision_path));
        explain_append(engine, "Conditions explained: %zu", engine->stats.conditions_explained);
        explain_append(engine, "Consequences explained: %zu", engine->stats.consequences_explained);
        explain_append(engine, "Rules explained: %zu", engine->stats.rules_explained);
        explain_append(engine, "Alternative paths considered: %zu", engine->stats.alternatives_considered);
        explain_append(engine, "Errors detected: %zu", engine->stats.errors_detected);
    }
}

/* Explanation access */
const char *explain_get_output(const explain_engine_t *engine)
{
    return engine ? engine->output : NULL;
}

const explain_stats_t *explain_get_stats(const explain_engine_t *engine)
{
    return engine ? &engine->stats : NULL;
}

/* Internal generation functions */
static void explain_generate_why(explain_engine_t *engine)
{
    if (engine->target_node) {
        const char *desc = get_node_description(engine->target_node);
        explain_append(engine, "Explanation for: %s\n", desc);
    } else {
        explain_append(engine, "Why Explanation\n");
    }
    
    explain_append(engine, "===================\n");
    
    /* Explain the decision path */
    explain_decision_path(engine);
    
    /* Explain key condition evaluations */
    explain_condition_evaluations(engine);
    
    /* Explain consequence executions */
    explain_consequence_executions(engine);
    
    /* Explain rule activations */
    explain_rule_activations(engine);
}

static void explain_generate_why_not(explain_engine_t *engine)
{
    if (engine->target_node) {
        const char *desc = get_node_description(engine->target_node);
        explain_append(engine, "Why Not Explanation for: %s\n", desc);
    } else {
        explain_append(engine, "Why Not Explanation\n");
    }
    
    explain_append(engine, "=======================\n");
    
    /* Explain the decision path */
    explain_decision_path(engine);
    
    /* Explain where the path diverged */
    if (engine->target_node) {
        const trace_entry_t *entry = find_first_entry(engine, TRACE_DECISION_BRANCH, engine->target_node);
        
        if (entry) {
            const ast_node_t *decision = entry->node;
            if (decision && decision->type == AST_DECISION) {
                bool took_true = was_condition_true(engine, decision);
                const char *branch_taken = took_true ? "TRUE" : "FALSE";
                const char *branch_needed = took_true ? "FALSE" : "TRUE";
                
                explain_append(engine, "\nPath diverged at decision: \"%s\"", 
                              decision->data.decision.condition);
                explain_append(engine, "- Took %s branch instead of required %s branch", 
                              branch_taken, branch_needed);
                engine->stats.alternatives_considered++;
            }
        }
    }
    
    /* Explain alternative paths that could lead to target */
    explain_alternative_paths(engine);
    
    /* Explain any errors that prevented execution */
    explain_errors(engine);
}

static void explain_generate_full(explain_engine_t *engine)
{
    explain_append(engine, "Full Explanation\n");
    explain_append(engine, "================\n");
    
    /* Explain the decision path */
    explain_decision_path(engine);
    
    /* Explain key condition evaluations */
    explain_condition_evaluations(engine);
    
    /* Explain consequence executions */
    explain_consequence_executions(engine);
    
    /* Explain rule activations */
    explain_rule_activations(engine);
    
    /* Explain alternative paths */
    explain_alternative_paths(engine);
    
    /* Explain any errors */
    explain_errors(engine);
}

/* Explanation components */
static void explain_decision_path(explain_engine_t *engine)
{
    if (!engine->trace) return;
    
    explain_append(engine, "\nDecision Path:\n");
    explain_append(engine, "--------------");
    
    const trace_entry_t *entry = trace_first(engine->trace);
    int last_depth = -1;
    bool in_decision = false;
    ast_node_t *last_decision = NULL;
    
    while (entry) {
        switch (entry->type) {
            case TRACE_ENTER_NODE:
                if (entry->node && entry->node->type == AST_DECISION) {
                    in_decision = true;
                    last_decision = entry->node;
                    vector_push(engine->decision_path, entry->node);
                    
                    explain_append(engine, "Decision: \"%s\"", 
                                  entry->node->data.decision.condition);
                    last_depth = entry->depth;
                }
                break;
                
            case TRACE_DECISION_BRANCH:
                if (in_decision && entry->node == last_decision) {
                    const char *branch = entry->value.data.bool_val ? "TRUE" : "FALSE";
                    explain_append(engine, "- Took %s branch", branch);
                    in_decision = false;
                }
                break;
                
            case TRACE_CONSEQUENCE_EXEC:
                if (entry->node && entry->node->type == AST_CONSEQUENCE) {
                    const char *status = entry->value.data.bool_val ? "SUCCESS" : "FAILED";
                    explain_append(engine, "Consequence: \"%s\" (%s)", 
                                  entry->node->data.consequence.action, status);
                }
                break;
                
            default:
                break;
        }
        
        entry = trace_next(engine->trace);
    }
    
    /* If we have a specific target, highlight it in the path */
    if (engine->target_node) {
        for (size_t i = 0; i < vector_size(engine->decision_path); i++) {
            ast_node_t *node = vector_get(engine->decision_path, i);
            if (node == engine->target_node) {
                explain_append(engine, "--> Target decision: \"%s\"", 
                              node->data.decision.condition);
                engine->stats.decisions_explained++;
            }
        }
    }
}

static void explain_condition_evaluations(explain_engine_t *engine)
{
    if (!engine->trace) return;
    
    explain_append(engine, "\nKey Condition Evaluations:\n");
    explain_append(engine, "-------------------------");
    
    const trace_entry_t *entry = trace_first(engine->trace);
    while (entry) {
        if (entry->type == TRACE_CONDITION_EVAL && entry->node) {
            /* Skip if we've already explained this node */
            if (hash_contains(engine->visited_nodes, (uintptr_t)entry->node)) {
                entry = trace_next(engine->trace);
                continue;
            }
            
            char value_str[128];
            reasons_value_to_string(&entry->value, value_str, sizeof(value_str));
            
            if (entry->node->type == AST_DECISION) {
                explain_append(engine, "Decision \"%s\" evaluated to: %s", 
                              entry->node->data.decision.condition, value_str);
            } else {
                explain_append(engine, "Condition evaluated to: %s", value_str);
            }
            
            hash_set(engine->visited_nodes, (uintptr_t)entry->node, (void*)1);
            engine->stats.conditions_explained++;
        }
        entry = trace_next(engine->trace);
    }
}

static void explain_consequence_executions(explain_engine_t *engine)
{
    if (!engine->trace) return;
    
    explain_append(engine, "\nConsequence Executions:\n");
    explain_append(engine, "----------------------");
    
    const trace_entry_t *entry = trace_first(engine->trace);
    while (entry) {
        if (entry->type == TRACE_CONSEQUENCE_EXEC && entry->node) {
            /* Skip if we've already explained this node */
            if (hash_contains(engine->visited_nodes, (uintptr_t)entry->node)) {
                entry = trace_next(engine->trace);
                continue;
            }
            
            const char *status = entry->value.data.bool_val ? "executed" : "not executed";
            explain_append(engine, "Consequence \"%s\" was %s", 
                          entry->node->data.consequence.action, status);
            
            /* For "why not" mode, explain why a specific consequence wasn't executed */
            if (engine->mode == EXPLAIN_WHY_NOT && 
                engine->target_node == entry->node &&
                !entry->value.data.bool_val) {
                const trace_entry_t *decision_entry = find_decision_entry(engine, entry->node);
                if (decision_entry && decision_entry->node) {
                    explain_append(engine, "- Because decision \"%s\" took the %s branch", 
                                  decision_entry->node->data.decision.condition,
                                  decision_entry->value.data.bool_val ? "TRUE" : "FALSE");
                }
            }
            
            hash_set(engine->visited_nodes, (uintptr_t)entry->node, (void*)1);
            engine->stats.consequences_explained++;
        }
        entry = trace_next(engine->trace);
    }
}

static void explain_rule_activations(explain_engine_t *engine)
{
    if (!engine->trace) return;
    
    explain_append(engine, "\nRule Activations:\n");
    explain_append(engine, "-----------------");
    
    const trace_entry_t *entry = trace_first(engine->trace);
    while (entry) {
        if (entry->type == TRACE_RULE_INVOKE && entry->node) {
            /* Skip if we've already explained this node */
            if (hash_contains(engine->visited_nodes, (uintptr_t)entry->node)) {
                entry = trace_next(engine->trace);
                continue;
            }
            
            char result_str[128] = "no result";
            if (entry->has_value) {
                reasons_value_to_string(&entry->value, result_str, sizeof(result_str));
            }
            
            explain_append(engine, "Rule \"%s\" executed with result: %s", 
                          entry->node->data.rule.name, result_str);
            
            hash_set(engine->visited_nodes, (uintptr_t)entry->node, (void*)1);
            engine->stats.rules_explained++;
        }
        entry = trace_next(engine->trace);
    }
}

static void explain_alternative_paths(explain_engine_t *engine)
{
    if (!engine->target_node || !engine->root) return;
    
    /* Only relevant for why-not explanations */
    if (engine->mode != EXPLAIN_WHY_NOT) return;
    
    explain_append(engine, "\nAlternative Paths to Target:\n");
    explain_append(engine, "----------------------------");
    
    /* Simple implementation: For decisions in the path, show what would happen
     * if we took the other branch */
    for (size_t i = 0; i < vector_size(engine->decision_path); i++) {
        ast_node_t *decision = vector_get(engine->decision_path, i);
        if (!was_condition_true(engine, decision)) {
            const char *alt_branch = "TRUE";
            ast_node_t *alt_consequence = decision->data.decision.true_branch;
            
            explain_append(engine, "If decision \"%s\" took %s branch:", 
                          decision->data.decision.condition, alt_branch);
            
            if (alt_consequence) {
                if (alt_consequence == engine->target_node) {
                    explain_append(engine, "- Would directly lead to target");
                } else if (alt_consequence->type == AST_CONSEQUENCE) {
                    explain_append(engine, "- Would execute consequence: \"%s\"", 
                                  alt_consequence->data.consequence.action);
                }
            }
            
            engine->stats.alternatives_considered++;
        }
    }
    
    /* Special case for golf mode */
    if (engine->golf_mode && engine->target_node && 
        engine->target_node->type == AST_CONSEQUENCE) {
        explain_append(engine, "\nGolf Mode Note:");
        explain_append(engine, "In golf mode, consequences can be triggered by:");
        explain_append(engine, "- Single-character identifiers (W: win, L: lose, D: draw)");
        explain_append(engine, "- Shorthand conditionals (x>5?win:lose)");
        explain_append(engine, "Ensure your consequence matches one of these patterns");
    }
}

static void explain_errors(explain_engine_t *engine)
{
    if (!engine->trace) return;
    
    explain_append(engine, "\nErrors Detected:\n");
    explain_append(engine, "---------------");
    
    const trace_entry_t *entry = trace_first(engine->trace);
    bool errors_found = false;
    
    while (entry) {
        if (entry->type == TRACE_ERROR_OCCURRED) {
            explain_append(engine, "- %s", entry->message);
            errors_found = true;
            engine->stats.errors_detected++;
        }
        entry = trace_next(engine->trace);
    }
    
    if (!errors_found) {
        explain_append(engine, "No errors detected during execution");
    }
}

/* Helper functions */
static void explain_append(explain_engine_t *engine, const char *format, ...)
{
    if (!engine) return;
    
    va_list args;
    va_start(args, format);
    
    /* Calculate required space */
    int needed = vsnprintf(NULL, 0, format, args);
    if (needed < 0) {
        va_end(args);
        return;
    }
    
    /* Ensure we have enough space */
    size_t new_len = engine->output_len + needed + 2;  // +1 for \n, +1 for \0
    if (new_len > engine->output_size) {
        size_t new_size = (new_len * 3) / 2;  // 1.5x growth factor
        char *new_output = memory_reallocate(engine->output, new_size);
        if (!new_output) {
            va_end(args);
            return;
        }
        engine->output = new_output;
        engine->output_size = new_size;
    }
    
    /* Append new content */
    char *buffer = engine->output + engine->output_len;
    vsnprintf(buffer, needed + 1, format, args);
    engine->output_len += needed;
    
    /* Add newline */
    if (engine->output_len < engine->output_size - 1) {
        engine->output[engine->output_len++] = '\n';
        engine->output[engine->output_len] = '\0';
    }
    
    va_end(args);
}

static const trace_entry_t *find_first_entry(explain_engine_t *engine, 
                                           trace_entry_type_t type, 
                                           const ast_node_t *node)
{
    if (!engine->trace) return NULL;
    
    trace_rewind(engine->trace);
    const trace_entry_t *entry = trace_first(engine->trace);
    
    while (entry) {
        if (entry->type == type && (!node || entry->node == node)) {
            return entry;
        }
        entry = trace_next(engine->trace);
    }
    
    return NULL;
}

static const trace_entry_t *find_decision_entry(explain_engine_t *engine, 
                                              const ast_node_t *consequence_node)
{
    if (!engine->trace || !consequence_node) return NULL;
    
    /* Find the consequence execution */
    const trace_entry_t *consequence_entry = find_first_entry(
        engine, TRACE_CONSEQUENCE_EXEC, consequence_node);
    
    if (!consequence_entry) return NULL;
    
    /* Walk backward to find the preceding decision */
    const trace_entry_t *entry = consequence_entry;
    while (entry) {
        if (entry->type == TRACE_DECISION_BRANCH) {
            return entry;
        }
        /* Move to previous entry - we don't have back links, so we need to start over */
        // This is inefficient but ok for small traces
        trace_rewind(engine->trace);
        const trace_entry_t *prev = NULL;
        const trace_entry_t *current = trace_first(engine->trace);
        while (current && current != entry) {
            prev = current;
            current = trace_next(engine->trace);
        }
        entry = prev;
    }
    
    return NULL;
}

static bool was_condition_true(explain_engine_t *engine, 
                              const ast_node_t *decision_node)
{
    const trace_entry_t *entry = find_first_entry(engine, TRACE_CONDITION_EVAL, decision_node);
    if (entry && entry->has_value && entry->value.type == VALUE_BOOL) {
        return entry->value.data.bool_val;
    }
    return false;
}

static bool was_consequence_executed(explain_engine_t *engine, 
                                    const ast_node_t *consequence_node)
{
    const trace_entry_t *entry = find_first_entry(engine, TRACE_CONSEQUENCE_EXEC, consequence_node);
    if (entry && entry->has_value && entry->value.type == VALUE_BOOL) {
        return entry->value.data.bool_val;
    }
    return false;
}

static bool was_rule_executed(explain_engine_t *engine, 
                             const ast_node_t *rule_node)
{
    return find_first_entry(engine, TRACE_RULE_INVOKE, rule_node) != NULL;
}

static const char *get_node_description(const ast_node_t *node)
{
    if (!node) return "Unknown node";
    
    static char buffer[256];
    
    switch (node->type) {
        case AST_DECISION:
            snprintf(buffer, sizeof(buffer), "Decision: \"%s\"", 
                    node->data.decision.condition);
            break;
        case AST_CONSEQUENCE:
            snprintf(buffer, sizeof(buffer), "Consequence: \"%s\"", 
                    node->data.consequence.action);
            break;
        case AST_RULE:
            snprintf(buffer, sizeof(buffer), "Rule: \"%s\"", 
                    node->data.rule.name);
            break;
        case AST_LOGIC_OP:
            snprintf(buffer, sizeof(buffer), "Logic Operation: %s", 
                    node->data.logic_op.op == LOGIC_AND ? "AND" :
                    node->data.logic_op.op == LOGIC_OR ? "OR" : "NOT");
            break;
        case AST_COMPARISON:
            snprintf(buffer, sizeof(buffer), "Comparison: %s", 
                    node->data.comparison.op == CMP_EQ ? "==" :
                    node->data.comparison.op == CMP_NE ? "!=" :
                    node->data.comparison.op == CMP_LT ? "<" :
                    node->data.comparison.op == CMP_LE ? "<=" :
                    node->data.comparison.op == CMP_GT ? ">" : ">=");
            break;
        case AST_IDENTIFIER:
            snprintf(buffer, sizeof(buffer), "Identifier: \"%s\"", 
                    node->data.identifier.name);
            break;
        case AST_LITERAL:
            {
                char value_str[128];
                reasons_value_to_string(&node->data.literal.value, value_str, sizeof(value_str));
                snprintf(buffer, sizeof(buffer), "Literal: %s", value_str);
            }
            break;
        case AST_CHAIN:
            snprintf(buffer, sizeof(buffer), "Chain: %s", 
                    node->data.chain.chain_type == CHAIN_SEQUENTIAL ? "sequential" : "parallel");
            break;
        default:
            snprintf(buffer, sizeof(buffer), "%s node", ast_node_type_name(node->type));
            break;
    }
    
    return buffer;
}

/* Golf mode explanations */
void explain_set_golf_mode(explain_engine_t *engine, bool golf_mode)
{
    if (engine) engine->golf_mode = golf_mode;
}

void explain_add_golf_shortcut(explain_engine_t *engine, const char *shortcut, const char *meaning)
{
    if (!engine || !shortcut || !meaning) return;
    
    explain_append(engine, "Golf Note: '%s' means %s", shortcut, meaning);
}

/* Explanation statistics */
void explain_reset(explain_engine_t *engine)
{
    if (!engine) return;
    
    engine->output_len = 0;
    memset(&engine->stats, 0, sizeof(explain_stats_t));
    vector_clear(engine->decision_path);
    hash_clear(engine->visited_nodes);
    engine->target_node = NULL;
}

/* Focused explanations */
void explain_focus_node(explain_engine_t *engine, const ast_node_t *node)
{
    if (engine) engine->target_node = node;
}

const char *explain_get_focused_output(explain_engine_t *engine, const ast_node_t *node)
{
    if (!engine || !node) return NULL;
    
    /* Save current state */
    const ast_node_t *prev_target = engine->target_node;
    
    /* Generate focused explanation */
    engine->target_node = node;
    explain_generate(engine, engine->root, engine->trace);
    
    /* Restore state */
    engine->target_node = prev_target;
    
    return engine->output;
}

/* Export functions */
bool explain_export_text(explain_engine_t *engine, const char *filename)
{
    if (!engine || !filename || !engine->output) return false;
    
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        error_set(ERROR_IO, "Failed to open explanation output file");
        return false;
    }
    
    fputs(engine->output, fp);
    fclose(fp);
    return true;
}

/* Debugging */
void explain_print(const explain_engine_t *engine, FILE *fp)
{
    if (!engine || !engine->output) return;
    if (!fp) fp = stdout;
    
    fputs(engine->output, fp);
}
