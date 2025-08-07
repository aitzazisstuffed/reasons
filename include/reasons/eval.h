#ifndef REASONS_EVAL_H
#define REASONS_EVAL_H

#include "reasons/ast.h"
#include "reasons/types.h"
#include "reasons/trace.h"
#include <stdbool.h>
#include <stddef.h>

/* Maximum recursion depth constant */
#define EVAL_MAX_RECURSION_DEPTH 1000

/* Evaluation statistics structure */
typedef struct {
    size_t nodes_evaluated;             /* Total AST nodes evaluated */
    size_t decisions_evaluated;         /* Decision nodes processed */
    size_t consequences_executed;       /* Consequences executed */
    size_t successful_consequences;     /* Consequences that succeeded */
    size_t failed_consequences;         /* Consequences that failed */
    size_t rules_executed;              /* Rules executed */
    size_t cache_hits;                  /* Memoization cache hits */
    size_t cache_misses;                /* Memoization cache misses */
} eval_stats_t;

/* Opaque evaluation context */
typedef struct eval_context eval_context_t;

/* Context management */
eval_context_t *eval_context_create(runtime_env_t *env);
void eval_context_destroy(eval_context_t *ctx);

/* Configuration */
void eval_set_tracing(eval_context_t *ctx, bool enabled);
void eval_set_explanation(eval_context_t *ctx, bool enabled);
void eval_set_golf_mode(eval_context_t *ctx, bool enabled);
void eval_set_max_recursion(eval_context_t *ctx, unsigned max_depth);

/* Main evaluation API */
reasons_value_t eval_tree(eval_context_t *ctx, ast_node_t *root);

/* Result inspection */
eval_stats_t eval_get_stats(const eval_context_t *ctx);
const trace_t *eval_get_trace(const eval_context_t *ctx);
const char *eval_get_explanation(const eval_context_t *ctx);

/* Golf mode */
bool eval_is_golf_mode(const eval_context_t *ctx);

/* Error handling */
bool eval_had_error(const eval_context_t *ctx);
const char *eval_get_error(const eval_context_t *ctx);

#endif /* REASONS_EVAL_H */
