/*
 * tree.c - Decision Tree Structures for Reasons DSL
 * 
 * Enhanced features:
 * - Multi-type tree nodes (condition, action, outcome)
 * - Node metadata for debugging/explanation
 * - Tree optimization capabilities
 * - Parent pointers for traversal
 * - Execution statistics tracking
 * - Path-sensitive analysis
 * - Integration with AST and evaluation components
 */

#include "reasons/tree.h"
#include "reasons/ast.h"
#include "reasons/eval.h"
#include "reasons/explain.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/collections.h"
#include "stdlib/stats.h"
#include <string.h>
#include <math.h>

/* Tree node structure */
struct TreeNode {
    NodeType type;
    char *id;           // Unique identifier
    char *description;  // Human-readable description
    int line;           // Source line number
    int column;         // Source column number
    
    // Execution statistics
    unsigned execution_count;
    double true_probability;
    double false_probability;
    double avg_exec_time;
    
    // Tree relationships
    TreeNode *parent;
    TreeNode *true_branch;
    TreeNode *false_branch;
    
    union {
        struct {
            AST_Node *condition;  // Condition expression AST
            double weight;         // Weight for probabilistic decisions
        } cond;
        
        struct {
            Vector *actions;       // Vector of consequence actions
            consequence_type_t type;
        } action;
        
        struct {
            reasons_value_t value; // Outcome value
            char *explanation;     // Generated explanation
        } outcome;
    };
};

/* Tree context structure */
struct DecisionTree {
    TreeNode *root;
    char *name;             // Tree name/identifier
    Vector *variables;       // Context variables
    Vector *node_registry;   // All nodes for fast access
    bool is_optimized;      // Optimization status
    
    // Statistics
    unsigned total_nodes;
    unsigned max_depth;
    double avg_exec_time;
};

/* ======== PRIVATE HELPER FUNCTIONS ======== */

static void node_free(TreeNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_CONDITION:
            if (node->cond.condition) ast_destroy(node->cond.condition);
            break;
            
        case NODE_ACTION:
            if (node->action.actions) {
                for (size_t i = 0; i < vector_size(node->action.actions); i++) {
                    ast_destroy(vector_at(node->action.actions, i));
                }
                vector_free(node->action.actions);
            }
            break;
            
        case NODE_OUTCOME:
            reasons_value_free(&node->outcome.value);
            if (node->outcome.explanation) mem_free(node->outcome.explanation);
            break;
    }
    
    if (node->id) mem_free(node->id);
    if (node->description) mem_free(node->description);
    mem_free(node);
}

static TreeNode* node_clone(const TreeNode *src) {
    if (!src) return NULL;
    
    TreeNode *node = mem_alloc(sizeof(TreeNode));
    if (!node) return NULL;
    
    // Copy basic fields
    *node = *src;
    node->id = src->id ? string_duplicate(src->id) : NULL;
    node->description = src->description ? string_duplicate(src->description) : NULL;
    node->parent = NULL;
    node->true_branch = NULL;
    node->false_branch = NULL;

    // Deep copy type-specific data
    switch (src->type) {
        case NODE_CONDITION:
            node->cond.condition = ast_clone(src->cond.condition);
            break;
            
        case NODE_ACTION:
            if (src->action.actions) {
                node->action.actions = vector_create();
                for (size_t i = 0; i < vector_size(src->action.actions); i++) {
                    AST_Node *action = ast_clone(vector_at(src->action.actions, i));
                    vector_append(node->action.actions, action);
                }
            }
            break;
            
        case NODE_OUTCOME:
            node->outcome.value = reasons_value_clone(&src->outcome.value);
            node->outcome.explanation = src->outcome.explanation ? 
                string_duplicate(src->outcome.explanation) : NULL;
            break;
    }
    
    // Recursively clone branches
    if (src->true_branch) {
        node->true_branch = node_clone(src->true_branch);
        node->true_branch->parent = node;
    }
    
    if (src->false_branch) {
        node->false_branch = node_clone(src->false_branch);
        node->false_branch->parent = node;
    }
    
    return node;
}

static void tree_build_registry(DecisionTree *tree, TreeNode *node) {
    if (!node) return;
    
    vector_append(tree->node_registry, node);
    
    if (node->type == NODE_CONDITION) {
        tree_build_registry(tree, node->true_branch);
        tree_build_registry(tree, node->false_branch);
    }
}

static void node_update_stats(TreeNode *node, bool branch_taken, double exec_time) {
    const double alpha = 0.2; // Smoothing factor
    
    node->execution_count++;
    node->avg_exec_time = (1 - alpha) * node->avg_exec_time + alpha * exec_time;
    
    if (node->type == NODE_CONDITION) {
        if (branch_taken) {
            node->true_probability = (1 - alpha) * node->true_probability + alpha;
            node->false_probability *= (1 - alpha);
        } else {
            node->false_probability = (1 - alpha) * node->false_probability + alpha;
            node->true_probability *= (1 - alpha);
        }
    }
}

static bool optimize_condition(TreeNode *node) {
    // Optimize constant conditions
    if (node->cond.condition && node->cond.condition->type == AST_LITERAL) {
        reasons_value_t val = node->cond.condition->data.literal.value;
        
        if (val.type == VALUE_BOOL) {
            TreeNode *target = val.data.bool_val ? node->true_branch : node->false_branch;
            
            if (target) {
                // Replace condition with target branch
                node_free(val.data.bool_val ? node->false_branch : node->true_branch);
                *node = *target;
                mem_free(target);
                return true;
            }
        }
    }
    return false;
}

static void optimize_tree_recursive(TreeNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_CONDITION:
            optimize_condition(node);
            optimize_tree_recursive(node->true_branch);
            optimize_tree_recursive(node->false_branch);
            break;
            
        case NODE_ACTION:
        case NODE_OUTCOME:
            // Leaf nodes - nothing to optimize
            break;
    }
}

/* ======== PUBLIC API IMPLEMENTATION ======== */

/* Tree creation/destruction */
DecisionTree* tree_create(const char *name) {
    DecisionTree *tree = mem_alloc(sizeof(DecisionTree));
    if (tree) {
        tree->root = NULL;
        tree->name = name ? string_duplicate(name) : NULL;
        tree->variables = vector_create();
        tree->node_registry = vector_create();
        tree->is_optimized = false;
        tree->total_nodes = 0;
        tree->max_depth = 0;
        tree->avg_exec_time = 0.0;
    }
    return tree;
}

void tree_destroy(DecisionTree *tree) {
    if (!tree) return;
    
    node_free(tree->root);
    if (tree->name) mem_free(tree->name);
    
    // Free variables
    for (size_t i = 0; i < vector_size(tree->variables); i++) {
        Variable *var = vector_at(tree->variables, i);
        if (var->name) mem_free(var->name);
        reasons_value_free(&var->value);
        mem_free(var);
    }
    vector_free(tree->variables);
    
    vector_free(tree->node_registry);
    mem_free(tree);
}

/* Node creation */
TreeNode* tree_create_condition_node(AST_Node *condition, double weight) {
    if (!condition) return NULL;
    
    TreeNode *node = mem_alloc(sizeof(TreeNode));
    if (node) {
        memset(node, 0, sizeof(TreeNode));
        node->type = NODE_CONDITION;
        node->cond.condition = condition;
        node->cond.weight = weight;
    }
    return node;
}

TreeNode* tree_create_action_node(Vector *actions, consequence_type_t type) {
    if (!actions || vector_size(actions) == 0) return NULL;
    
    TreeNode *node = mem_alloc(sizeof(TreeNode));
    if (node) {
        memset(node, 0, sizeof(TreeNode));
        node->type = NODE_ACTION;
        node->action.actions = actions;
        node->action.type = type;
    }
    return node;
}

TreeNode* tree_create_outcome_node(const reasons_value_t *value) {
    TreeNode *node = mem_alloc(sizeof(TreeNode));
    if (node) {
        memset(node, 0, sizeof(TreeNode));
        node->type = NODE_OUTCOME;
        node->outcome.value = reasons_value_clone(value);
    }
    return node;
}

/* Tree manipulation */
void tree_set_root(DecisionTree *tree, TreeNode *root) {
    if (!tree) return;
    
    if (tree->root) node_free(tree->root);
    tree->root = root;
    tree->is_optimized = false;
    
    // Rebuild registry
    vector_clear(tree->node_registry);
    tree_build_registry(tree, root);
    tree->total_nodes = vector_size(tree->node_registry);
}

void tree_add_variable(DecisionTree *tree, const char *name, reasons_value_t value) {
    if (!tree || !name) return;
    
    // Check if variable exists
    for (size_t i = 0; i < vector_size(tree->variables); i++) {
        Variable *var = vector_at(tree->variables, i);
        if (strcmp(var->name, name) == 0) {
            reasons_value_free(&var->value);
            var->value = reasons_value_clone(&value);
            return;
        }
    }
    
    // Create new variable
    Variable *var = mem_alloc(sizeof(Variable));
    if (var) {
        var->name = string_duplicate(name);
        var->value = reasons_value_clone(&value);
        vector_append(tree->variables, var);
    }
}

TreeNode* tree_find_node(DecisionTree *tree, const char *id) {
    if (!tree || !id) return NULL;
    
    for (size_t i = 0; i < vector_size(tree->node_registry); i++) {
        TreeNode *node = vector_at(tree->node_registry, i);
        if (node->id && strcmp(node->id, id) == 0) {
            return node;
        }
    }
    return NULL;
}

/* Tree traversal */
void tree_traverse(TreeNode *root, TreeVisitor visit, void *context) {
    if (!root || !visit) return;
    
    // Pre-order traversal
    visit(root, context);
    
    if (root->type == NODE_CONDITION) {
        tree_traverse(root->true_branch, visit, context);
        tree_traverse(root->false_branch, visit, context);
    }
}

void tree_traverse_path(TreeNode *node, PathVisitor visit, void *context) {
    if (!node || !visit) return;
    
    Vector *path = vector_create();
    while (node) {
        vector_insert(path, 0, node);
        node = node->parent;
    }
    
    for (size_t i = 0; i < vector_size(path); i++) {
        visit(vector_at(path, i), i, context);
    }
    
    vector_free(path);
}

/* Tree evaluation */
reasons_value_t tree_evaluate(DecisionTree *tree, runtime_env_t *env, 
                              explain_engine_t *explainer, trace_t *trace) {
    reasons_value_t result = {VALUE_NULL};
    if (!tree || !tree->root) return result;
    
    TreeNode *current = tree->root;
    while (current) {
        // Update execution statistics
        double start_time = runtime_current_time(env);
        
        switch (current->type) {
            case NODE_CONDITION: {
                // Evaluate condition
                eval_context_t *ctx = eval_context_create(env);
                reasons_value_t cond_val = eval_node(ctx, current->cond.condition);
                bool cond_result = is_truthy(&cond_val);
                
                // Update statistics
                double exec_time = runtime_current_time(env) - start_time;
                node_update_stats(current, cond_result, exec_time);
                
                // Trace and explain
                if (trace) trace_condition(trace, current, cond_result);
                if (explainer) explain_condition(explainer, current, cond_result);
                
                // Move to next node
                current = cond_result ? current->true_branch : current->false_branch;
                reasons_value_free(&cond_val);
                eval_context_destroy(ctx);
                break;
            }
            
            case NODE_ACTION: {
                // Execute all actions
                for (size_t i = 0; i < vector_size(current->action.actions); i++) {
                    AST_Node *action = vector_at(current->action.actions, i);
                    consequence_result_t cr = runtime_execute_consequence(
                        env, 
                        action,
                        current->action.type
                    );
                    
                    // Update result
                    if (cr.success && cr.value) {
                        result = *cr.value;
                    }
                    
                    // Update statistics
                    double exec_time = runtime_current_time(env) - start_time;
                    node_update_stats(current, cr.success, exec_time);
                    
                    // Trace and explain
                    if (trace) trace_consequence(trace, current, cr.success);
                    if (explainer) explain_consequence(explainer, current, cr.success);
                }
                current = NULL; // Actions are leaf nodes
                break;
            }
            
            case NODE_OUTCOME: {
                result = reasons_value_clone(&current->outcome.value);
                
                // Generate explanation if needed
                if (explainer && !current->outcome.explanation) {
                    current->outcome.explanation = explain_generate(explainer, NULL, NULL);
                }
                
                // Update statistics
                double exec_time = runtime_current_time(env) - start_time;
                node_update_stats(current, true, exec_time);
                
                // Trace
                if (trace) trace_outcome(trace, current);
                
                current = NULL; // Outcomes are leaf nodes
                break;
            }
        }
    }
    
    return result;
}

/* Tree cloning */
DecisionTree* tree_clone(const DecisionTree *src) {
    if (!src) return NULL;
    
    DecisionTree *tree = tree_create(src->name);
    if (tree) {
        tree->root = node_clone(src->root);
        tree->is_optimized = src->is_optimized;
        
        // Clone variables
        for (size_t i = 0; i < vector_size(src->variables); i++) {
            Variable *var_src = vector_at(src->variables, i);
            tree_add_variable(tree, var_src->name, var_src->value);
        }
        
        // Rebuild registry
        tree_build_registry(tree, tree->root);
    }
    return tree;
}

/* Tree optimization */
void tree_optimize(DecisionTree *tree) {
    if (!tree || tree->is_optimized) return;
    
    optimize_tree_recursive(tree->root);
    tree->is_optimized = true;
    
    // Rebuild registry after optimization
    vector_clear(tree->node_registry);
    tree_build_registry(tree, tree->root);
    tree->total_nodes = vector_size(tree->node_registry);
}

/* Tree statistics */
TreeStatistics tree_get_statistics(const DecisionTree *tree) {
    TreeStatistics stats = {0};
    if (!tree) return stats;
    
    stats.total_nodes = tree->total_nodes;
    stats.condition_nodes = 0;
    stats.action_nodes = 0;
    stats.outcome_nodes = 0;
    
    for (size_t i = 0; i < vector_size(tree->node_registry); i++) {
        TreeNode *node = vector_at(tree->node_registry, i);
        switch (node->type) {
            case NODE_CONDITION: stats.condition_nodes++; break;
            case NODE_ACTION: stats.action_nodes++; break;
            case NODE_OUTCOME: stats.outcome_nodes++; break;
        }
    }
    
    return stats;
}

/* Tree serialization */
void tree_serialize(const TreeNode *node, SerializeCallback callback, void *context) {
    if (!node || !callback) return;
    
    switch (node->type) {
        case NODE_CONDITION:
            callback(node, "condition", context);
            if (node->true_branch) tree_serialize(node->true_branch, callback, context);
            if (node->false_branch) tree_serialize(node->false_branch, callback, context);
            break;
            
        case NODE_ACTION:
            callback(node, "action", context);
            break;
            
        case NODE_OUTCOME:
            callback(node, "outcome", context);
            break;
    }
}
