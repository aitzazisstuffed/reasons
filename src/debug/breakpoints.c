/*
 * breakpoints.c - Conditional Breakpoint Management for Reasons Debugger
 * 
 * Features:
 * - Node-based breakpoints (unconditional)
 * - Expression-based conditional breakpoints
 * - Hit count tracking and conditions
 * - Breakpoint enable/disable management
 * - Temporary breakpoints
 * - Integration with debugger core
 */

#include "reasons/debugger.h"
#include "reasons/eval.h"
#include "reasons/ast.h"
#include "reasons/tree.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/string_utils.h"
#include "utils/collections.h"
#include <string.h>
#include <stdlib.h>

/* Breakpoint structure */
struct Breakpoint {
    char *id;                   // Unique breakpoint ID
    char *node_id;              // Associated tree node ID
    AST_Node *condition;        // Conditional expression
    unsigned hit_count;         // Number of times hit
    unsigned hit_limit;         // Stop after N hits (0 = infinite)
    bool enabled;               // Enabled state
    bool temporary;             // Temporary (one-shot) breakpoint
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static bool evaluate_condition(DebuggerState *dbg, AST_Node *condition) {
    if (!condition) return true; // No condition = always true
    
    reasons_value_t result = eval_node(dbg->eval_ctx, condition);
    bool should_break = is_truthy(&result);
    reasons_value_free(&result);
    
    return should_break;
}

static Breakpoint* find_breakpoint_by_id(DebuggerState *dbg, const char *id) {
    if (!id) return NULL;
    
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        if (strcmp(bp->id, id) == 0) {
            return bp;
        }
    }
    return NULL;
}

static char* generate_bp_id(const char *node_id) {
    static unsigned counter = 1;
    size_t len = snprintf(NULL, 0, "bp-%s-%u", node_id ? node_id : "global", counter);
    char *id = mem_alloc(len + 1);
    if (id) {
        snprintf(id, len + 1, "bp-%s-%u", node_id ? node_id : "global", counter++);
    }
    return id;
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

Breakpoint* breakpoint_create(DebuggerState *dbg, const char *node_id, 
                             AST_Node *condition, bool enabled) {
    if (!dbg || !node_id) return NULL;
    
    Breakpoint *bp = mem_alloc(sizeof(Breakpoint));
    if (!bp) return NULL;
    
    bp->id = generate_bp_id(node_id);
    bp->node_id = string_duplicate(node_id);
    bp->condition = condition;
    bp->hit_count = 0;
    bp->hit_limit = 0; // No limit
    bp->enabled = enabled;
    bp->temporary = false;
    
    if (!bp->id || !bp->node_id) {
        if (bp->id) mem_free(bp->id);
        if (bp->node_id) mem_free(bp->node_id);
        mem_free(bp);
        return NULL;
    }
    
    LOG_DEBUG("Created breakpoint %s for node %s", bp->id, bp->node_id);
    return bp;
}

void breakpoint_destroy(Breakpoint *bp) {
    if (!bp) return;
    
    if (bp->id) mem_free(bp->id);
    if (bp->node_id) mem_free(bp->node_id);
    if (bp->condition) ast_destroy(bp->condition);
    mem_free(bp);
}

void breakpoint_add(DebuggerState *dbg, Breakpoint *bp) {
    if (!dbg || !bp) return;
    
    // Check for existing breakpoint on same node
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *existing = vector_at(dbg->breakpoints, i);
        if (strcmp(existing->node_id, bp->node_id) == 0) {
            LOG_WARN("Replacing existing breakpoint on node %s", bp->node_id);
            vector_remove(dbg->breakpoints, i);
            breakpoint_destroy(existing);
            break;
        }
    }
    
    vector_append(dbg->breakpoints, bp);
    LOG_INFO("Added breakpoint %s for node %s", bp->id, bp->node_id);
}

Breakpoint* breakpoint_remove(DebuggerState *dbg, const char *id) {
    if (!dbg || !id) return NULL;
    
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        if (strcmp(bp->id, id) == 0) {
            vector_remove(dbg->breakpoints, i);
            LOG_INFO("Removed breakpoint %s", id);
            return bp;
        }
    }
    return NULL;
}

Breakpoint* breakpoint_find(DebuggerState *dbg, const char *node_id) {
    if (!dbg || !node_id) return NULL;
    
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        if (strcmp(bp->node_id, node_id) == 0) {
            return bp;
        }
    }
    return NULL;
}

void breakpoint_enable(DebuggerState *dbg, const char *id) {
    Breakpoint *bp = find_breakpoint_by_id(dbg, id);
    if (bp) {
        bp->enabled = true;
        LOG_INFO("Enabled breakpoint %s", id);
    }
}

void breakpoint_disable(DebuggerState *dbg, const char *id) {
    Breakpoint *bp = find_breakpoint_by_id(dbg, id);
    if (bp) {
        bp->enabled = false;
        LOG_INFO("Disabled breakpoint %s", id);
    }
}

void breakpoint_set_condition(DebuggerState *dbg, const char *id, AST_Node *condition) {
    Breakpoint *bp = find_breakpoint_by_id(dbg, id);
    if (bp) {
        if (bp->condition) ast_destroy(bp->condition);
        bp->condition = condition;
        LOG_INFO("Set condition for breakpoint %s", id);
    }
}

void breakpoint_set_hit_limit(DebuggerState *dbg, const char *id, unsigned limit) {
    Breakpoint *bp = find_breakpoint_by_id(dbg, id);
    if (bp) {
        bp->hit_limit = limit;
        LOG_INFO("Set hit limit for breakpoint %s to %u", id, limit);
    }
}

bool breakpoint_should_break(DebuggerState *dbg, TreeNode *node) {
    if (!dbg || !node || !node->id) return false;
    
    // Find breakpoints for this node
    bool should_break = false;
    Breakpoint *triggered_bp = NULL;
    
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        if (strcmp(bp->node_id, node->id) == 0 && bp->enabled) {
            if (evaluate_condition(dbg, bp->condition)) {
                bp->hit_count++;
                should_break = true;
                triggered_bp = bp;
                
                // Check hit limit
                if (bp->hit_limit > 0 && bp->hit_count >= bp->hit_limit) {
                    LOG_INFO("Breakpoint %s reached hit limit (%u), disabling", 
                            bp->id, bp->hit_limit);
                    bp->enabled = false;
                }
                break;
            }
        }
    }
    
    // Handle temporary breakpoints
    if (triggered_bp && triggered_bp->temporary) {
        LOG_INFO("Temporary breakpoint %s triggered, removing", triggered_bp->id);
        vector_remove_element(dbg->breakpoints, triggered_bp);
        breakpoint_destroy(triggered_bp);
    }
    
    return should_break;
}

void breakpoint_list(DebuggerState *dbg, FILE *output) {
    if (!dbg || !output) return;
    
    if (vector_size(dbg->breakpoints) == 0) {
        fprintf(output, "No breakpoints set\n");
        return;
    }
    
    fprintf(output, "Breakpoints:\n");
    fprintf(output, "  %-8s %-6s %-8s %-12s %-20s %s\n", 
            "ID", "State", "Hits", "Hit Limit", "Node", "Condition");
    fprintf(output, "  -----------------------------------------------------------------\n");
    
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        fprintf(output, "  %-8s %-6s %-8u %-12u %-20s ",
                bp->id,
                bp->enabled ? "enabled" : "disabled",
                bp->hit_count,
                bp->hit_limit,
                bp->node_id);
        
        if (bp->condition) {
            // Print simplified condition
            char buffer[128];
            ast_print_compact(bp->condition, buffer, sizeof(buffer));
            fprintf(output, "%s", buffer);
        } else {
            fprintf(output, "unconditional");
        }
        
        fprintf(output, "\n");
    }
}

Breakpoint* breakpoint_add_temporary(DebuggerState *dbg, const char *node_id) {
    Breakpoint *bp = breakpoint_create(dbg, node_id, NULL, true);
    if (bp) {
        bp->temporary = true;
        breakpoint_add(dbg, bp);
    }
    return bp;
}

void breakpoint_reset_hit_counts(DebuggerState *dbg) {
    if (!dbg) return;
    
    for (size_t i = 0; i < vector_size(dbg->breakpoints); i++) {
        Breakpoint *bp = vector_at(dbg->breakpoints, i);
        bp->hit_count = 0;
    }
    LOG_INFO("Reset all breakpoint hit counts");
}
