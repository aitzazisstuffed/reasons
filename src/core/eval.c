/*
 * eval.c - Expression Evaluator and Decision Engine for Reasons DSL
 * 
 * Implements the core evaluation logic for decision trees with tracing and explanation.
 * Features:
 * - Recursive AST evaluation
 * - Context-sensitive execution
 * - Detailed tracing for debugging
 * - Explanation engine integration
 * - Golf mode optimizations
 * - Side-effect tracking
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "reasons/eval.h"
#include "reasons/ast.h"
#include "reasons/trace.h"
#include "reasons/explain.h"
#include "reasons/runtime.h"
#include "reasons/types.h"
#include "utils/error.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "stdlib/math.h"
#include "stdlib/string.h"
#include "stdlib/stats.h"

/* Evaluation context structure */
struct eval_context {
    runtime_env_t *env;             /* Runtime environment */
    trace_t *trace;                 /* Execution tracer */
    explain_engine_t *explainer;    /* Explanation engine */
    hash_table_t *cache;            /* Result cache for memoization */
    vector_t *call_stack;           /* Function call stack */
    bool golf_mode;                 /* Enable golf optimizations */
    bool tracing_enabled;           /* Control tracing */
    bool explanation_mode;          /* Generate explanations */
    unsigned recursion_depth;       /* Current recursion depth */
    unsigned max_recursion_depth;   /* Maximum allowed depth */
    eval_stats_t stats;             /* Evaluation statistics */
};

/* Forward declarations */
static reasons_value_t eval_node(eval_context_t *ctx, ast_node_t *node);
static reasons_value_t eval_decision(eval_context_t *ctx, ast_node_t *node);
static reasons_value_t eval_consequence(eval_context_t *ctx, ast_node_t *node);
static reasons_value_t eval_rule(eval_context_t *ctx, ast_node_t *node);
static reasons_value_t eval_logic_op(eval_context_t *ctx, ast_node_t *node);
static reasons_value_t eval_comparison(eval_context_t *ctx, ast_node_t *node);
static reasons_value_t eval_chain(eval_context_t *ctx, ast_node_t *node);
static reasons_value_t eval_ternary(eval_context_t *ctx, ast_node_t *node);
static bool is_truthy(const reasons_value_t *value);
static bool is_equal(const reasons_value_t *a, const reasons_value_t *b);

/* Context creation/destruction */
eval_context_t *eval_context_create(runtime_env_t *env)
{
    eval_context_t *ctx = memory_allocate(sizeof(eval_context_t));
    if (!ctx) {
        error_set(ERROR_MEMORY, "Failed to allocate evaluation context");
        return NULL;
    }

    memset(ctx, 0, sizeof(eval_context_t));
    ctx->env = env;
    ctx->trace = trace_create();
    ctx->explainer = explain_create();
    ctx->cache = hash_create(64);
    ctx->call_stack = vector_create(16);
    ctx->max_recursion_depth = EVAL_MAX_RECURSION_DEPTH;
    ctx->tracing_enabled = true;
    ctx->explanation_mode = true;
    
    /* Default to environment's golf mode if available */
    if (env) {
        ctx->golf_mode = runtime_get_option(env, RUNTIME_OPTION_GOLF_MODE);
    } else {
        ctx->golf_mode = true;
    }

    return ctx;
}

void eval_context_destroy(eval_context_t *ctx)
{
    if (!ctx) return;
    
    trace_destroy(ctx->trace);
    explain_destroy(ctx->explainer);
    hash_destroy(ctx->cache);
    vector_destroy(ctx->call_stack);
    memory_free(ctx);
}

/* Configuration */
void eval_set_tracing(eval_context_t *ctx, bool enabled)
{
    if (ctx) ctx->tracing_enabled = enabled;
}

void eval_set_explanation(eval_context_t *ctx, bool enabled)
{
    if (ctx) ctx->explanation_mode = enabled;
}

void eval_set_golf_mode(eval_context_t *ctx, bool enabled)
{
    if (ctx) ctx->golf_mode = enabled;
}

void eval_set_max_recursion(eval_context_t *ctx, unsigned max_depth)
{
    if (ctx) ctx->max_recursion_depth = max_depth;
}

/* Main evaluation entry point */
reasons_value_t eval_tree(eval_context_t *ctx, ast_node_t *root)
{
    if (!ctx || !root) {
        reasons_value_t null_value = {VALUE_NULL};
        return null_value;
    }

    /* Reset evaluation state */
    ctx->recursion_depth = 0;
    memset(&ctx->stats, 0, sizeof(eval_stats_t));
    trace_clear(ctx->trace);
    explain_reset(ctx->explainer);

    /* Start evaluation */
    trace_begin(ctx->trace, "Main Evaluation");
    reasons_value_t result = eval_node(ctx, root);
    trace_end(ctx->trace);

    /* Generate explanation if enabled */
    if (ctx->explanation_mode) {
        explain_generate(ctx->explainer, root, ctx->trace);
    }

    return result;
}

/* Core recursive evaluation */
static reasons_value_t eval_node(eval_context_t *ctx, ast_node_t *node)
{
    if (!node) {
        reasons_value_t null_value = {VALUE_NULL};
        return null_value;
    }

    /* Check recursion limit */
    if (ctx->recursion_depth >= ctx->max_recursion_depth) {
        error_set(ERROR_EVAL_RECURSION, "Maximum recursion depth exceeded");
        reasons_value_t error_value = {VALUE_ERROR};
        return error_value;
    }

    ctx->recursion_depth++;
    ctx->stats.nodes_evaluated++;

    /* Trace node entry */
    if (ctx->tracing_enabled) {
        trace_enter_node(ctx->trace, node);
    }

    reasons_value_t result;
    memset(&result, 0, sizeof(result));

    /* Dispatch based on node type */
    switch (node->type) {
        case AST_DECISION:
            result = eval_decision(ctx, node);
            break;
        case AST_CONSEQUENCE:
            result = eval_consequence(ctx, node);
            break;
        case AST_RULE:
            result = eval_rule(ctx, node);
            break;
        case AST_LOGIC_OP:
            result = eval_logic_op(ctx, node);
            break;
        case AST_COMPARISON:
            result = eval_comparison(ctx, node);
            break;
        case AST_IDENTIFIER:
            result = runtime_get_variable(ctx->env, node->data.identifier.name);
            break;
        case AST_LITERAL:
            result = node->data.literal.value;
            break;
        case AST_CHAIN:
            result = eval_chain(ctx, node);
            break;
        case AST_PROGRAM:
            /* Evaluate all children, return last result */
            {
                ast_node_t *child = node->first_child;
                reasons_value_t last_result = {VALUE_NULL};
                while (child) {
                    last_result = eval_node(ctx, child);
                    child = child->next_sibling;
                }
                result = last_result;
            }
            break;
        default:
            error_set(ERROR_EVAL_UNSUPPORTED, "Unsupported AST node type");
            result.type = VALUE_ERROR;
            break;
    }

    /* Trace node exit */
    if (ctx->tracing_enabled) {
        trace_exit_node(ctx->trace, node, &result);
    }

    ctx->recursion_depth--;
    return result;
}

/* Decision node evaluation */
static reasons_value_t eval_decision(eval_context_t *ctx, ast_node_t *node)
{
    reasons_value_t result = {VALUE_NULL};
    bool condition_result = false;
    bool condition_evaluated = false;
    
    /* Evaluate condition - first child is the condition expression */
    ast_node_t *condition_node = node->first_child;
    if (condition_node) {
        reasons_value_t cond_value = eval_node(ctx, condition_node);
        
        /* Record condition evaluation */
        if (ctx->explanation_mode) {
            explain_condition(ctx->explainer, node, &cond_value);
        }
        
        condition_result = is_truthy(&cond_value);
        condition_evaluated = true;
        
        /* Free any temporary values */
        reasons_value_free(&cond_value);
    } else {
        error_set(ERROR_EVAL_SYNTAX, "Decision node missing condition");
    }
    
    /* Determine which branch to take */
    ast_node_t *branch = condition_result ? 
        node->data.decision.true_branch : 
        node->data.decision.false_branch;
    
    /* Execute branch if exists */
    if (branch) {
        result = eval_node(ctx, branch);
    } else {
        /* No branch executed - return condition result */
        result.type = VALUE_BOOL;
        result.data.bool_val = condition_result;
    }
    
    /* Record decision outcome */
    if (condition_evaluated && ctx->explanation_mode) {
        explain_decision(ctx->explainer, node, condition_result, branch);
    }
    
    return result;
}

/* Consequence evaluation */
static reasons_value_t eval_consequence(eval_context_t *ctx, ast_node_t *node)
{
    /* Execute consequence action */
    consequence_result_t cr = runtime_execute_consequence(
        ctx->env, 
        node->data.consequence.action,
        node->data.consequence.type
    );
    
    /* Record execution */
    if (ctx->tracing_enabled) {
        trace_consequence(ctx->trace, node, cr.success);
    }
    
    /* Update statistics */
    ctx->stats.consequences_executed++;
    if (cr.success) {
        ctx->stats.successful_consequences++;
    } else {
        ctx->stats.failed_consequences++;
    }
    
    /* Create result value */
    reasons_value_t result;
    result.type = VALUE_BOOL;
    result.data.bool_val = cr.success;
    
    /* Store additional result info if available */
    if (cr.message) {
        result.type = VALUE_STRING;
        result.data.string_val = string_duplicate(cr.message);
    } else if (cr.value) {
        result = *cr.value;
    }
    
    return result;
}

/* Rule evaluation */
static reasons_value_t eval_rule(eval_context_t *ctx, ast_node_t *node)
{
    if (!node->data.rule.is_active) {
        reasons_value_t false_value = {VALUE_BOOL, .data.bool_val = false};
        return false_value;
    }
    
    /* Check recursion (prevent infinite loops) */
    if (vector_contains(ctx->call_stack, node)) {
        error_set(ERROR_EVAL_RECURSION, "Rule recursion detected");
        reasons_value_t error_value = {VALUE_ERROR};
        return error_value;
    }
    
    /* Add to call stack */
    vector_push(ctx->call_stack, node);
    
    /* Execute rule body */
    reasons_value_t result = eval_node(ctx, node->data.rule.body);
    
    /* Remove from call stack */
    vector_pop(ctx->call_stack);
    
    /* Update rule stats */
    node->data.rule.execution_count++;
    ctx->stats.rules_executed++;
    
    /* Record execution */
    if (ctx->tracing_enabled) {
        trace_rule_execution(ctx->trace, node, &result);
    }
    
    return result;
}

/* Logical operation evaluation */
static reasons_value_t eval_logic_op(eval_context_t *ctx, ast_node_t *node)
{
    reasons_value_t left_val = eval_node(ctx, node->data.logic_op.left);
    reasons_value_t result = {VALUE_BOOL, .data.bool_val = false};
    
    switch (node->data.logic_op.op) {
        case LOGIC_NOT:
            result.data.bool_val = !is_truthy(&left_val);
            break;
            
        case LOGIC_AND:
            /* Short-circuit evaluation */
            if (!is_truthy(&left_val)) {
                result.data.bool_val = false;
            } else {
                reasons_value_t right_val = eval_node(ctx, node->data.logic_op.right);
                result.data.bool_val = is_truthy(&right_val);
                reasons_value_free(&right_val);
            }
            break;
            
        case LOGIC_OR:
            /* Short-circuit evaluation */
            if (is_truthy(&left_val)) {
                result.data.bool_val = true;
            } else {
                reasons_value_t right_val = eval_node(ctx, node->data.logic_op.right);
                result.data.bool_val = is_truthy(&right_val);
                reasons_value_free(&right_val);
            }
            break;
            
        default:
            error_set(ERROR_EVAL_UNSUPPORTED, "Unsupported logic operation");
            result.type = VALUE_ERROR;
            break;
    }
    
    reasons_value_free(&left_val);
    return result;
}

/* Comparison evaluation */
static reasons_value_t eval_comparison(eval_context_t *ctx, ast_node_t *node)
{
    reasons_value_t left_val = eval_node(ctx, node->data.comparison.left);
    reasons_value_t right_val = eval_node(ctx, node->data.comparison.right);
    reasons_value_t result = {VALUE_BOOL, .data.bool_val = false};
    
    /* Numeric comparisons */
    if (left_val.type == VALUE_NUMBER && right_val.type == VALUE_NUMBER) {
        double left = left_val.data.number_val;
        double right = right_val.data.number_val;
        
        switch (node->data.comparison.op) {
            case CMP_EQ:  result.data.bool_val = (left == right); break;
            case CMP_NE:  result.data.bool_val = (left != right); break;
            case CMP_LT:  result.data.bool_val = (left < right); break;
            case CMP_LE:  result.data.bool_val = (left <= right); break;
            case CMP_GT:  result.data.bool_val = (left > right); break;
            case CMP_GE:  result.data.bool_val = (left >= right); break;
            default: break;
        }
    }
    /* String comparisons */
    else if (left_val.type == VALUE_STRING && right_val.type == VALUE_STRING) {
        int cmp = strcmp(left_val.data.string_val, right_val.data.string_val);
        
        switch (node->data.comparison.op) {
            case CMP_EQ:  result.data.bool_val = (cmp == 0); break;
            case CMP_NE:  result.data.bool_val = (cmp != 0); break;
            case CMP_LT:  result.data.bool_val = (cmp < 0); break;
            case CMP_LE:  result.data.bool_val = (cmp <= 0); break;
            case CMP_GT:  result.data.bool_val = (cmp > 0); break;
            case CMP_GE:  result.data.bool_val = (cmp >= 0); break;
            default: break;
        }
    }
    /* Boolean comparisons */
    else if (left_val.type == VALUE_BOOL && right_val.type == VALUE_BOOL) {
        bool left = left_val.data.bool_val;
        bool right = right_val.data.bool_val;
        
        switch (node->data.comparison.op) {
            case CMP_EQ:  result.data.bool_val = (left == right); break;
            case CMP_NE:  result.data.bool_val = (left != right); break;
            default: 
                error_set(ERROR_EVAL_TYPE, "Invalid operation for booleans");
                result.type = VALUE_ERROR;
                break;
        }
    }
    /* Type mismatch */
    else {
        error_set(ERROR_EVAL_TYPE, "Type mismatch in comparison");
        result.type = VALUE_ERROR;
    }
    
    reasons_value_free(&left_val);
    reasons_value_free(&right_val);
    return result;
}

/* Chain operator evaluation */
static reasons_value_t eval_chain(eval_context_t *ctx, ast_node_t *node)
{
    reasons_value_t first_result = eval_node(ctx, node->data.chain.first);
    reasons_value_t second_result = {VALUE_NULL};
    
    /* Handle golf mode optimization: skip second if first fails */
    if (ctx->golf_mode && !is_truthy(&first_result)) {
        reasons_value_free(&first_result);
        reasons_value_t false_value = {VALUE_BOOL, .data.bool_val = false};
        return false_value;
    }
    
    /* Evaluate second part */
    second_result = eval_node(ctx, node->data.chain.second);
    
    /* Determine combined result */
    reasons_value_t result = {VALUE_BOOL};
    switch (node->data.chain.chain_type) {
        case CHAIN_SEQUENTIAL:
            /* Both must succeed */
            result.data.bool_val = is_truthy(&first_result) && 
                                  is_truthy(&second_result);
            break;
            
        case CHAIN_PARALLEL:
            /* Either can succeed */
            result.data.bool_val = is_truthy(&first_result) || 
                                  is_truthy(&second_result);
            break;
            
        default:
            result.data.bool_val = false;
            break;
    }
    
    reasons_value_free(&first_result);
    reasons_value_free(&second_result);
    return result;
}

/* Helper functions */
static bool is_truthy(const reasons_value_t *value)
{
    if (!value) return false;
    
    switch (value->type) {
        case VALUE_BOOL:   return value->data.bool_val;
        case VALUE_NUMBER: return value->data.number_val != 0.0;
        case VALUE_STRING: return value->data.string_val[0] != '\0';
        case VALUE_NULL:   return false;
        case VALUE_ERROR:  return false;
        default:           return true;
    }
}

static bool is_equal(const reasons_value_t *a, const reasons_value_t *b)
{
    if (a->type != b->type) return false;
    
    switch (a->type) {
        case VALUE_BOOL:   return a->data.bool_val == b->data.bool_val;
        case VALUE_NUMBER: return fabs(a->data.number_val - b->data.number_val) < 1e-9;
        case VALUE_STRING: return strcmp(a->data.string_val, b->data.string_val) == 0;
        case VALUE_NULL:   return true;
        case VALUE_ERROR:  return false;
        default:           return false;
    }
}

/* Statistics and debugging */
eval_stats_t eval_get_stats(const eval_context_t *ctx)
{
    if (ctx) return ctx->stats;
    
    eval_stats_t empty_stats = {0};
    return empty_stats;
}

const trace_t *eval_get_trace(const eval_context_t *ctx)
{
    return ctx ? ctx->trace : NULL;
}

const char *eval_get_explanation(const eval_context_t *ctx)
{
    return ctx ? explain_get_output(ctx->explainer) : NULL;
}

/* Golf mode helpers */
bool eval_is_golf_mode(const eval_context_t *ctx)
{
    return ctx ? ctx->golf_mode : false;
}

/* Error handling */
bool eval_had_error(const eval_context_t *ctx)
{
    return ctx ? (error_last() != ERROR_NONE) : false;
}

const char *eval_get_error(const eval_context_t *ctx)
{
    return error_message(error_last());
}
