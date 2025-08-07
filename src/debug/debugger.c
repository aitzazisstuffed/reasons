/*
 * debugger.c - Interactive Debugger Core for Reasons DSL
 * 
 * Features:
 * - Breakpoint management (conditional/unconditional)
 * - Step-by-step execution control
 * - Variable inspection and modification
 * - Call stack navigation
 * - Watch expressions
 * - Decision history tracking
 * - Branch coverage analysis
 * - Integration with explanation engine
 */

#include "reasons/debugger.h"
#include "reasons/eval.h"
#include "reasons/trace.h"
#include "reasons/explain.h"
#include "reasons/tree.h"
#include "reasons/runtime.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "utils/string_utils.h"
#include "stdlib/string.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>

/* Debugger state structure */
struct DebuggerState {
    runtime_env_t *env;             // Runtime environment
    eval_context_t *eval_ctx;       // Evaluation context
    trace_t *trace;                 // Execution trace
    explain_engine_t *explainer;    // Explanation engine
    DecisionTree *tree;             // Current decision tree
    
    vector_t *breakpoints;          // List of breakpoints
    vector_t *watch_exprs;          // Watch expressions
    vector_t *decision_history;     // Decision path history
    
    bool is_running;                // Execution running state
    bool step_mode;                 // Step-by-step mode
    bool verbose;                   // Verbose output
    unsigned current_node;          // Current node in trace
    
    coverage_data_t coverage;       // Branch coverage data
};

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

/* Command handler function type */
typedef void (*CommandHandler)(DebuggerState *dbg, const char *args);

/* Command structure */
typedef struct {
    const char *name;               // Command name
    CommandHandler handler;         // Handler function
    const char *help;               // Help text
    const char *usage;              // Usage example
} DebuggerCommand;

/* ======== FORWARD DECLARATIONS ======== */
static void cmd_help(DebuggerState *dbg, const char *args);
static void cmd_break(DebuggerState *dbg, const char *args);
static void cmd_run(DebuggerState *dbg, const char *args);
static void cmd_step(DebuggerState *dbg, const char *args);
static void cmd_next(DebuggerState *dbg, const char *args);
static void cmd_continue(DebuggerState *dbg, const char *args);
static void cmd_print(DebuggerState *dbg, const char *args);
static void cmd_watch(DebuggerState *dbg, const char *args);
static void cmd_backtrace(DebuggerState *dbg, const char *args);
static void cmd_coverage(DebuggerState *dbg, const char *args);
static void cmd_explain(DebuggerState *dbg, const char *args);
static void cmd_history(DebuggerState *dbg, const char *args);
static void cmd_quit(DebuggerState *dbg, const char *args);

/* Command table */
static const DebuggerCommand commands[] = {
    {"help", cmd_help, "Show help information", "help [command]"},
    {"break", cmd_break, "Set breakpoint", "break <node_id> [condition]"},
    {"run", cmd_run, "Start execution", "run"},
    {"step", cmd_step, "Step into next node", "step"},
    {"next", cmd_next, "Step over next node", "next"},
    {"continue", cmd_continue, "Continue execution", "continue"},
    {"print", cmd_print, "Print expression", "print <expression>"},
    {"watch", cmd_watch, "Add watch expression", "watch <expression>"},
    {"backtrace", cmd_backtrace, "Show call stack", "backtrace"},
    {"coverage", cmd_coverage, "Show coverage info", "coverage"},
    {"explain", cmd_explain, "Explain current decision", "explain"},
    {"history", cmd_history, "Show decision history", "history"},
    {"quit", cmd_quit, "Exit debugger", "quit"},
    {NULL, NULL, NULL, NULL} // Sentinel
};

/* ======== DEBUGGER CORE FUNCTIONS ======== */

DebuggerState* debugger_create(runtime_env_t *env, eval_context_t *eval_ctx,
                              DecisionTree *tree) {
    DebuggerState *dbg = mem_alloc(sizeof(DebuggerState));
    if (dbg) {
        memset(dbg, 0, sizeof(DebuggerState));
        dbg->env = env;
        dbg->eval_ctx = eval_ctx;
        dbg->tree = tree;
        dbg->trace = trace_create();
        dbg->explainer = explain_create();
        dbg->breakpoints = vector_create(8);
        dbg->watch_exprs = vector_create(8);
        dbg->decision_history = vector_create(32);
        dbg->verbose = true;
        
        // Initialize coverage
        memset(&dbg->coverage, 0, sizeof(coverage_data_t));
    }
    return dbg;
}

void debugger_destroy(DebuggerState *dbg) {
    if (!dbg) return;
    
    // Free breakpoints
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        if (bp->node_id) mem_free(bp->node_id);
        if (bp->condition) ast_destroy(bp->condition);
        mem_free(bp);
    }
    vector_destroy(dbg->breakpoints);
    
    // Free watch expressions
    for (size_t i = 0; i < vector_size(dbg->watch_exprs); i++) {
        WatchExpr *we = vector_at(dbg->watch_exprs, i);
        if (we->expr) mem_free(we->expr);
        if (we->parsed_expr) ast_destroy(we->parsed_expr);
        reasons_value_free(&we->last_value);
        mem_free(we);
    }
    vector_destroy(dbg->watch_exprs);
    
    // Free decision history
    vector_destroy(dbg->decision_history);
    
    trace_destroy(dbg->trace);
    explain_destroy(dbg->explainer);
    mem_free(dbg);
}

void debugger_run(DebuggerState *dbg) {
    if (!dbg || !dbg->tree) return;
    
    printf("Reasons Debugger - Type 'help' for commands\n");
    dbg->is_running = true;
    
    // Main debugger loop
    while (dbg->is_running) {
        char *input = readline("(reasons) ");
        if (!input) break; // EOF
        
        // Skip empty lines
        if (*input == '\0') {
            free(input);
            continue;
        }
        
        // Add to history
        add_history(input);
        
        // Parse command
        char *cmd = strtok(input, " ");
        char *args = strtok(NULL, "");
        
        // Find and execute command
        bool found = false;
        for (int i = 0; commands[i].name; i++) {
            if (strcmp(cmd, commands[i].name) == 0) {
                commands[i].handler(dbg, args ? args : "");
                found = true;
                break;
            }
        }
        
        if (!found) {
            printf("Unknown command: %s. Type 'help' for commands.\n", cmd);
        }
        
        free(input);
    }
}

/* ======== BREAKPOINT MANAGEMENT ======== */

Breakpoint* debugger_add_breakpoint(DebuggerState *dbg, const char *node_id, 
                                   AST_Node *condition) {
    if (!dbg || !node_id) return NULL;
    
    Breakpoint *bp = mem_alloc(sizeof(Breakpoint));
    if (bp) {
        bp->node_id = string_duplicate(node_id);
        bp->condition = condition;
        bp->enabled = true;
        bp->hit_count = 0;
        vector_append(dbg->breakpoints, bp);
    }
    return bp;
}

bool debugger_remove_breakpoint(DebuggerState *dbg, const char *node_id) {
    if (!dbg || !node_id) return false;
    
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        if (strcmp(bp->node_id, node_id) == 0) {
            vector_remove(dbg->breakpoints, i);
            if (bp->node_id) mem_free(bp->node_id);
            if (bp->condition) ast_destroy(bp->condition);
            mem_free(bp);
            return true;
        }
    }
    return false;
}

Breakpoint* debugger_find_breakpoint(DebuggerState *dbg, const char *node_id) {
    if (!dbg || !node_id) return NULL;
    
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        if (strcmp(bp->node_id, node_id) == 0) {
            return bp;
        }
    }
    return NULL;
}

bool debugger_check_breakpoint(DebuggerState *dbg, TreeNode *node) {
    if (!dbg || !node || !node->id) return false;
    
    Breakpoint *bp = debugger_find_breakpoint(dbg, node->id);
    if (!bp || !bp->enabled) return false;
    
    // Check condition if present
    if (bp->condition) {
        reasons_value_t result = eval_node(dbg->eval_ctx, bp->condition);
        bool should_break = is_truthy(&result);
        reasons_value_free(&result);
        if (!should_break) return false;
    }
    
    bp->hit_count++;
    return true;
}

/* ======== WATCH EXPRESSIONS ======== */

WatchExpr* debugger_add_watch(DebuggerState *dbg, const char *expr) {
    if (!dbg || !expr) return NULL;
    
    // Parse expression
    // (In a real implementation, this would use the parser)
    AST_Node *parsed = ast_create_identifier(expr); // Simplified
    
    WatchExpr *we = mem_alloc(sizeof(WatchExpr));
    if (we) {
        we->expr = string_duplicate(expr);
        we->parsed_expr = parsed;
        we->last_value.type = VALUE_NULL;
        vector_append(dbg->watch_exprs, we);
    }
    return we;
}

void debugger_update_watches(DebuggerState *dbg) {
    if (!dbg) return;
    
    for (size_t i = 0; i < vector_size(dbg->watch_exprs); i++) {
        WatchExpr *we = vector_at(dbg->watch_exprs, i);
        reasons_value_t new_value = eval_node(dbg->eval_ctx, we->parsed_expr);
        
        // Check if value changed
        if (!is_equal(&new_value, &we->last_value)) {
            printf("Watch %zu: %s = ", i, we->expr);
            reasons_value_print(&new_value, stdout);
            printf("\n");
            
            reasons_value_free(&we->last_value);
            we->last_value = new_value;
        } else {
            reasons_value_free(&new_value);
        }
    }
}

/* ======== COVERAGE TRACKING ======== */

void debugger_record_coverage(DebuggerState *dbg, TreeNode *node) {
    if (!dbg || !node) return;
    
    // Update coverage stats
    dbg->coverage.nodes_visited++;
    
    if (node->type == NODE_CONDITION) {
        dbg->coverage.branches_total += 2; // True and false branches
    }
    
    // Mark node as visited
    node->execution_count++;
}

void debugger_print_coverage(DebuggerState *dbg) {
    if (!dbg) return;
    
    size_t total_nodes = tree_total_nodes(dbg->tree);
    double node_coverage = total_nodes > 0 ? 
        (double)dbg->coverage.nodes_visited / total_nodes * 100.0 : 0.0;
    
    double branch_coverage = dbg->coverage.branches_total > 0 ?
        (double)dbg->coverage.branches_visited / dbg->coverage.branches_total * 100.0 : 0.0;
    
    printf("Coverage Report:\n");
    printf("  Nodes:    %zu/%zu (%.2f%%)\n", 
           dbg->coverage.nodes_visited, total_nodes, node_coverage);
    printf("  Branches: %zu/%zu (%.2f%%)\n",
           dbg->coverage.branches_visited, dbg->coverage.branches_total, branch_coverage);
}

/* ======== DECISION HISTORY ======== */

void debugger_record_history(DebuggerState *dbg, TreeNode *node, 
                            reasons_value_t decision) {
    if (!dbg || !node) return;
    
    DecisionEntry *entry = mem_alloc(sizeof(DecisionEntry));
    if (entry) {
        entry->node_id = node->id ? string_duplicate(node->id) : NULL;
        entry->timestamp = time(NULL);
        entry->decision = decision;
        vector_append(dbg->decision_history, entry);
    }
}

void debugger_print_history(DebuggerState *dbg, int max_entries) {
    if (!dbg) return;
    
    size_t start = max_entries > 0 && 
                   vector_size(dbg->decision_history) > (size_t)max_entries ?
        vector_size(dbg->decision_history) - max_entries : 0;
    
    printf("Decision History:\n");
    for (size_t i = start; i < vector_size(dbg->decision_history); i++) {
        DecisionEntry *entry = vector_at(dbg->decision_history, i);
        struct tm *tm = localtime(&entry->timestamp);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm);
        
        printf("  [%s] %s = ", time_buf, entry->node_id);
        reasons_value_print(&entry->decision, stdout);
        printf("\n");
    }
}

/* ======== COMMAND HANDLERS ======== */

static void cmd_help(DebuggerState *dbg, const char *args) {
    printf("Available commands:\n");
    for (int i = 0; commands[i].name; i++) {
        printf("  %-10s - %s\n", commands[i].name, commands[i].help);
        if (*args && strcmp(commands[i].name, args) == 0) {
            printf("      Usage: %s\n", commands[i].usage);
        }
    }
}

static void cmd_break(DebuggerState *dbg, const char *args) {
    if (!args || *args == '\0') {
        printf("Usage: break <node_id> [condition]\n");
        return;
    }
    
    // Parse arguments
    char *node_id = strtok((char*)args, " ");
    char *cond_str = strtok(NULL, "");
    
    AST_Node *condition = NULL;
    if (cond_str) {
        // Parse condition (simplified)
        condition = ast_create_identifier(cond_str);
    }
    
    if (debugger_add_breakpoint(dbg, node_id, condition)) {
        printf("Breakpoint set at node: %s\n", node_id);
    } else {
        printf("Failed to set breakpoint\n");
    }
}

static void cmd_run(DebuggerState *dbg, const char *args) {
    // Start evaluation
    trace_clear(dbg->trace);
    explain_reset(dbg->explainer);
    
    reasons_value_t result = tree_evaluate(dbg->tree, dbg->env, 
                                         dbg->explainer, dbg->trace);
    
    printf("Evaluation completed. Result: ");
    reasons_value_print(&result, stdout);
    printf("\n");
    
    // Update coverage
    debugger_print_coverage(dbg);
}

static void cmd_step(DebuggerState *dbg, const char *args) {
    if (!dbg->is_running) {
        printf("Execution not started. Use 'run' first.\n");
        return;
    }
    
    // Execute next node
    // (Implementation would integrate with tree evaluation)
    printf("Stepping to next node...\n");
    
    // Update watches
    debugger_update_watches(dbg);
}

static void cmd_next(DebuggerState *dbg, const char *args) {
    // Step over (don't enter child nodes)
    // (Implementation would integrate with tree evaluation)
    printf("Stepping over to next node...\n");
    
    // Update watches
    debugger_update_watches(dbg);
}

static void cmd_continue(DebuggerState *dbg, const char *args) {
    // Continue until next breakpoint
    // (Implementation would integrate with tree evaluation)
    printf("Continuing execution...\n");
    
    // Simulate hitting a breakpoint
    printf("Breakpoint hit at node: root_decision\n");
    
    // Update watches
    debugger_update_watches(dbg);
}

static void cmd_print(DebuggerState *dbg, const char *args) {
    if (!args || *args == '\0') {
        printf("Usage: print <expression>\n");
        return;
    }
    
    // Parse and evaluate expression
    // (Simplified - would use actual parser)
    AST_Node *expr = ast_create_identifier(args);
    reasons_value_t value = eval_node(dbg->eval_ctx, expr);
    
    printf("%s = ", args);
    reasons_value_print(&value, stdout);
    printf("\n");
    
    reasons_value_free(&value);
    ast_destroy(expr);
}

static void cmd_watch(DebuggerState *dbg, const char *args) {
    if (!args || *args == '\0') {
        printf("Usage: watch <expression>\n");
        return;
    }
    
    if (debugger_add_watch(dbg, args)) {
        printf("Added watch expression: %s\n", args);
    } else {
        printf("Failed to add watch expression\n");
    }
}

static void cmd_backtrace(DebuggerState *dbg, const char *args) {
    printf("Call Stack:\n");
    
    // Simulated stack frames
    printf("  #0: main_decision (line 42)\n");
    printf("  #1: loan_approval_rule (line 18)\n");
    printf("  #2: credit_score_check (line 5)\n");
}

static void cmd_coverage(DebuggerState *dbg, const char *args) {
    debugger_print_coverage(dbg);
}

static void cmd_explain(DebuggerState *dbg, const char *args) {
    const char *explanation = explain_get_output(dbg->explainer);
    if (explanation) {
        printf("Explanation:\n%s\n", explanation);
    } else {
        printf("No explanation available\n");
    }
}

static void cmd_history(DebuggerState *dbg, const char *args) {
    int max_entries = 10; // Default
    if (args && *args != '\0') {
        max_entries = atoi(args);
        if (max_entries <= 0) max_entries = 10;
    }
    
    debugger_print_history(dbg, max_entries);
}

static void cmd_quit(DebuggerState *dbg, const char *args) {
    dbg->is_running = false;
    printf("Exiting debugger\n");
}
