#ifndef REASONS_AST_H
#define REASONS_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "reasons/types.h"

// AST Node Types
typedef enum {
    AST_DECISION,
    AST_CONSEQUENCE,
    AST_RULE,
    AST_LOGIC_OP,
    AST_COMPARISON,
    AST_IDENTIFIER,
    AST_LITERAL,
    AST_CHAIN,
    AST_NODE_TYPE_COUNT
} ast_node_type_t;

// Condition Types
typedef enum {
    CONDITION_EXPRESSION,
    CONDITION_TYPE_COUNT
} condition_type_t;

// Consequence Types
typedef enum {
    CONSEQUENCE_ACTION,
    CONSEQUENCE_TYPE_COUNT
} consequence_type_t;

// Chain Types
typedef enum {
    CHAIN_SEQUENTIAL,
    CHAIN_PARALLEL,
    CHAIN_TYPE_COUNT
} chain_type_t;

// Logic Operations
typedef enum {
    LOGIC_AND,
    LOGIC_OR,
    LOGIC_NOT,
    LOGIC_OP_COUNT
} logic_op_t;

// Comparison Operations
typedef enum {
    CMP_EQ,
    CMP_NE,
    CMP_LT,
    CMP_LE,
    CMP_GT,
    CMP_GE,
    COMPARISON_OP_COUNT
} comparison_op_t;

// Forward declarations
typedef struct ast_node ast_node_t;

// Visitor and predicate function types
typedef bool (*ast_visitor_func_t)(ast_node_t *node, void *user_data);
typedef bool (*ast_predicate_func_t)(const ast_node_t *node, void *user_data);

// AST Statistics Structure
typedef struct {
    size_t total_nodes;
    size_t max_depth;
    size_t current_depth;
    size_t decisions_created;
    size_t consequences_created;
    size_t rules_created;
} ast_statistics_t;

// Node creation functions
ast_node_t *ast_create_node(ast_node_type_t type);
ast_node_t *ast_create_decision(const char *condition, ast_node_t *true_branch, 
                               ast_node_t *false_branch);
ast_node_t *ast_create_consequence(const char *action, consequence_type_t type);
ast_node_t *ast_create_rule(const char *name, ast_node_t *body);
ast_node_t *ast_create_logic_op(logic_op_t op, ast_node_t *left, ast_node_t *right);
ast_node_t *ast_create_comparison(comparison_op_t op, ast_node_t *left, ast_node_t *right);
ast_node_t *ast_create_identifier(const char *name);
ast_node_t *ast_create_literal(const reasons_value_t *value);
ast_node_t *ast_create_chain(ast_node_t *first, ast_node_t *second);

// Tree manipulation
bool ast_add_child(ast_node_t *parent, ast_node_t *child);
bool ast_remove_child(ast_node_t *parent, ast_node_t *child);
ast_node_t *ast_get_child(const ast_node_t *parent, size_t index);
size_t ast_get_child_count(const ast_node_t *parent);

// Traversal functions
void ast_traverse_preorder(ast_node_t *root, ast_visitor_func_t visitor, void *user_data);
void ast_traverse_postorder(ast_node_t *root, ast_visitor_func_t visitor, void *user_data);
ast_node_t *ast_find_node(ast_node_t *root, ast_predicate_func_t predicate, void *user_data);
void ast_clear_tree(ast_node_t **root);

// Cloning functions
ast_node_t *ast_clone(const ast_node_t *node);

// Comparison functions
bool ast_equals(const ast_node_t *a, const ast_node_t *b);

// Printing functions
void ast_print(const ast_node_t *root, FILE *fp);
void ast_print_compact(const ast_node_t *node, FILE *fp);

// Statistics and debugging
void ast_get_statistics(ast_statistics_t *stats);
void ast_reset_statistics(void);
const char *ast_node_type_name(ast_node_type_t type);

// Golf-specific optimizations
ast_node_t *ast_create_golf_shorthand(const char *expr);
ast_node_t *ast_optimize_for_golf(ast_node_t *root);

// Utility functions
ast_node_t *ast_create_if_then_else(const char *condition, 
                                   const char *then_action, 
                                   const char *else_action);
ast_node_t *ast_create_simple_rule(const char *rule_name, 
                                  const char *condition,
                                  const char *action);
size_t ast_get_depth(const ast_node_t *root);
size_t ast_count_nodes(const ast_node_t *root);
bool ast_validate(const ast_node_t *root);

// Cleanup functions
void ast_destroy(ast_node_t *root);

// Maximum depth for AST validation
#define AST_MAX_DEPTH 1024

#endif // REASONS_AST_H
