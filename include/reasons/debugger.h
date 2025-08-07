#ifndef REASONS_DEBUGGER_H
#define REASONS_DEBUGGER_H

#include "reasons/runtime.h"
#include "reasons/eval.h"
#include "reasons/tree.h"
#include "reasons/trace.h"
#include "reasons/explain.h"
#include "reasons/ast.h"
#include "reasons/types.h"
#include <stdbool.h>

/* Opaque debugger state structure */
typedef struct DebuggerState DebuggerState;

/* Breakpoint structure */
typedef struct {
    char *node_id;                  // Node identifier
    AST_Node *condition;            // Conditional expression
    bool enabled;                   // Enabled state
    unsigned hit_count;             // Number of times hit
} Breakpoint;

/* Watch expression structure */
typedef struct {
    char *expr;                     // Expression string
    AST_Node *parsed_expr;          // Parsed expression
    reasons_value_t last_value;      // Last computed value
} WatchExpr;

/* Decision history entry */
typedef struct {
    char *node_id;                  // Node identifier
    time_t timestamp;               // Time of decision
    reasons_value_t decision;        // Decision value
} DecisionEntry;

/* Coverage data */
typedef struct {
    size_t nodes_visited;           // Number of nodes visited
    size_t branches_visited;        // Number of branches traversed
    size_t branches_total;          // Total number of branches
} coverage_data_t;

/* Debugger creation/destruction */
DebuggerState* debugger_create(runtime_env_t *env, eval_context_t *eval_ctx,
                              DecisionTree *tree);
void debugger_destroy(DebuggerState *dbg);

/* Main debugger loop */
void debugger_run(DebuggerState *dbg);

/* Breakpoint management */
Breakpoint* debugger_add_breakpoint(DebuggerState *dbg, const char *node_id, 
                                   AST_Node *condition);
bool debugger_remove_breakpoint(DebuggerState *dbg, const char *node_id);
Breakpoint* debugger_find_breakpoint(DebuggerState *dbg, const char *node_id);
bool debugger_check_breakpoint(DebuggerState *dbg, TreeNode *node);

/* Watch expressions */
WatchExpr* debugger_add_watch(DebuggerState *dbg, const char *expr);
void debugger_update_watches(DebuggerState *dbg);

/* Coverage tracking */
void debugger_record_coverage(DebuggerState *dbg, TreeNode *node);
void debugger_print_coverage(DebuggerState *dbg);

/* Decision history */
void debugger_record_history(DebuggerState *dbg, TreeNode *node, 
                            reasons_value_t decision);
void debugger_print_history(DebuggerState *dbg, int max_entries);

/* Configuration */
void debugger_set_verbose(DebuggerState *dbg, bool verbose);
bool debugger_get_verbose(const DebuggerState *dbg);

/* Execution control */
void debugger_pause_execution(DebuggerState *dbg);
void debugger_resume_execution(DebuggerState *dbg);
void debugger_step_execution(DebuggerState *dbg);
void debugger_step_over_execution(DebuggerState *dbg);

/* Accessors */
eval_context_t* debugger_get_eval_context(DebuggerState *dbg);
trace_t* debugger_get_trace(DebuggerState *dbg);
explain_engine_t* debugger_get_explainer(DebuggerState *dbg);
DecisionTree* debugger_get_decision_tree(DebuggerState *dbg);

#endif /* REASONS_DEBUGGER_H */
