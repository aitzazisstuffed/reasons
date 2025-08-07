#ifndef REASONS_TREE_H
#define REASONS_TREE_H

#include "reasons/ast.h"
#include "reasons/runtime.h"
#include "reasons/explain.h"
#include "utils/collections.h"
#include <stdbool.h>

typedef enum {
    NODE_CONDITION,
    NODE_ACTION,
    NODE_OUTCOME
} NodeType;

typedef struct TreeNode TreeNode;
typedef struct DecisionTree DecisionTree;

typedef void (*TreeVisitor)(TreeNode *node, void *context);
typedef void (*PathVisitor)(TreeNode *node, size_t depth, void *context);
typedef void (*SerializeCallback)(const TreeNode *node, const char *type_str, void *context);

typedef struct {
    unsigned total_nodes;
    unsigned condition_nodes;
    unsigned action_nodes;
    unsigned outcome_nodes;
} TreeStatistics;

typedef struct {
    char *name;
    reasons_value_t value;
} Variable;

DecisionTree* tree_create(const char *name);
void tree_destroy(DecisionTree *tree);

TreeNode* tree_create_condition_node(AST_Node *condition, double weight);
TreeNode* tree_create_action_node(Vector *actions, consequence_type_t type);
TreeNode* tree_create_outcome_node(const reasons_value_t *value);

void tree_set_root(DecisionTree *tree, TreeNode *root);
void tree_add_variable(DecisionTree *tree, const char *name, reasons_value_t value);
TreeNode* tree_find_node(DecisionTree *tree, const char *id);

void tree_traverse(TreeNode *root, TreeVisitor visit, void *context);
void tree_traverse_path(TreeNode *node, PathVisitor visit, void *context);

reasons_value_t tree_evaluate(DecisionTree *tree, runtime_env_t *env, 
                              explain_engine_t *explainer, trace_t *trace);

DecisionTree* tree_clone(const DecisionTree *src);
void tree_optimize(DecisionTree *tree);

TreeStatistics tree_get_statistics(const DecisionTree *tree);
void tree_serialize(const TreeNode *node, SerializeCallback callback, void *context);

#endif
