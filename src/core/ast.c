/*
 * ast.c - Abstract Syntax Tree Implementation for Reasons DSL
 * 
 * This module implements the core AST data structures and operations for the
 * Reasons decision tree language. The AST is designed for golf-friendly syntax
 * while supporting complex decision logic, consequences, and rule chaining.
 * 
 * Key design principles:
 * - Compact representation for golf programming
 * - Support for decision trees with conditions and consequences
 * - Logic operators and rule chaining
 * - Memory-efficient node structures
 * - Easy traversal and evaluation
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reasons/ast.h"
#include "reasons/types.h"
#include "utils/error.h"
#include "utils/memory.h"
#include "utils/logger.h"

/* Global AST statistics for debugging and profiling */
static struct {
    size_t total_nodes;
    size_t max_depth;
    size_t current_depth;
    size_t decisions_created;
    size_t consequences_created;
    size_t rules_created;
} g_ast_stats = {0};

/* Forward declarations for internal functions */
static void ast_node_destroy_recursive(ast_node_t *node);
static size_t ast_calculate_depth(const ast_node_t *node);
static bool ast_validate_node_recursive(const ast_node_t *node, int depth);

/* AST Node Creation Functions */
ast_node_t *ast_create_node(ast_node_type_t type)
{
    ast_node_t *node = memory_allocate(sizeof(ast_node_t));
    if (!node) {
        error_set(ERROR_MEMORY, "Failed to allocate AST node");
        return NULL;
    }

    memset(node, 0, sizeof(ast_node_t));
    node->type = type;
    node->line = 0;
    node->column = 0;
    node->parent = NULL;
    node->next_sibling = NULL;
    node->first_child = NULL;

    g_ast_stats.total_nodes++;
    
    LOG_DEBUG("Created AST node of type %d", type);
    return node;
}

ast_node_t *ast_create_decision(const char *condition, ast_node_t *true_branch, 
                               ast_node_t *false_branch)
{
    if (!condition) {
        error_set(ERROR_INVALID_ARGUMENT, "Decision condition cannot be null");
        return NULL;
    }

    ast_node_t *node = ast_create_node(AST_DECISION);
    if (!node) {
        return NULL;
    }

    node->data.decision.condition = string_duplicate(condition);
    if (!node->data.decision.condition) {
        ast_destroy(node);
        error_set(ERROR_MEMORY, "Failed to duplicate condition string");
        return NULL;
    }

    node->data.decision.true_branch = true_branch;
    node->data.decision.false_branch = false_branch;
    node->data.decision.condition_type = CONDITION_EXPRESSION;
    node->data.decision.priority = 0;

    /* Set parent relationships */
    if (true_branch) {
        true_branch->parent = node;
    }
    if (false_branch) {
        false_branch->parent = node;
    }

    g_ast_stats.decisions_created++;
    return node;
}

ast_node_t *ast_create_consequence(const char *action, consequence_type_t type)
{
    if (!action) {
        error_set(ERROR_INVALID_ARGUMENT, "Consequence action cannot be null");
        return NULL;
    }

    ast_node_t *node = ast_create_node(AST_CONSEQUENCE);
    if (!node) {
        return NULL;
    }

    node->data.consequence.action = string_duplicate(action);
    if (!node->data.consequence.action) {
        ast_destroy(node);
        error_set(ERROR_MEMORY, "Failed to duplicate action string");
        return NULL;
    }

    node->data.consequence.type = type;
    node->data.consequence.weight = 1.0;
    node->data.consequence.executed = false;

    g_ast_stats.consequences_created++;
    return node;
}

ast_node_t *ast_create_rule(const char *name, ast_node_t *body)
{
    if (!name) {
        error_set(ERROR_INVALID_ARGUMENT, "Rule name cannot be null");
        return NULL;
    }

    ast_node_t *node = ast_create_node(AST_RULE);
    if (!node) {
        return NULL;
    }

    node->data.rule.name = string_duplicate(name);
    if (!node->data.rule.name) {
        ast_destroy(node);
        error_set(ERROR_MEMORY, "Failed to duplicate rule name");
        return NULL;
    }

    node->data.rule.body = body;
    node->data.rule.is_active = true;
    node->data.rule.execution_count = 0;

    if (body) {
        body->parent = node;
    }

    g_ast_stats.rules_created++;
    return node;
}

ast_node_t *ast_create_logic_op(logic_op_t op, ast_node_t *left, ast_node_t *right)
{
    ast_node_t *node = ast_create_node(AST_LOGIC_OP);
    if (!node) {
        return NULL;
    }

    node->data.logic_op.op = op;
    node->data.logic_op.left = left;
    node->data.logic_op.right = right;

    /* Set parent relationships */
    if (left) {
        left->parent = node;
    }
    if (right) {
        right->parent = node;
    }

    return node;
}

ast_node_t *ast_create_comparison(comparison_op_t op, ast_node_t *left, ast_node_t *right)
{
    ast_node_t *node = ast_create_node(AST_COMPARISON);
    if (!node) {
        return NULL;
    }

    node->data.comparison.op = op;
    node->data.comparison.left = left;
    node->data.comparison.right = right;

    /* Set parent relationships */
    if (left) {
        left->parent = node;
    }
    if (right) {
        right->parent = node;
    }

    return node;
}

ast_node_t *ast_create_identifier(const char *name)
{
    if (!name) {
        error_set(ERROR_INVALID_ARGUMENT, "Identifier name cannot be null");
        return NULL;
    }

    ast_node_t *node = ast_create_node(AST_IDENTIFIER);
    if (!node) {
        return NULL;
    }

    node->data.identifier.name = string_duplicate(name);
    if (!node->data.identifier.name) {
        ast_destroy(node);
        error_set(ERROR_MEMORY, "Failed to duplicate identifier name");
        return NULL;
    }

    return node;
}

ast_node_t *ast_create_literal(const reasons_value_t *value)
{
    if (!value) {
        error_set(ERROR_INVALID_ARGUMENT, "Literal value cannot be null");
        return NULL;
    }

    ast_node_t *node = ast_create_node(AST_LITERAL);
    if (!node) {
        return NULL;
    }

    /* Deep copy the value */
    node->data.literal.value = *value;
    if (value->type == VALUE_STRING && value->data.string_val) {
        node->data.literal.value.data.string_val = string_duplicate(value->data.string_val);
        if (!node->data.literal.value.data.string_val) {
            ast_destroy(node);
            error_set(ERROR_MEMORY, "Failed to duplicate string literal");
            return NULL;
        }
    }

    return node;
}

ast_node_t *ast_create_chain(ast_node_t *first, ast_node_t *second)
{
    ast_node_t *node = ast_create_node(AST_CHAIN);
    if (!node) {
        return NULL;
    }

    node->data.chain.first = first;
    node->data.chain.second = second;
    node->data.chain.chain_type = CHAIN_SEQUENTIAL;

    /* Set parent relationships */
    if (first) {
        first->parent = node;
    }
    if (second) {
        second->parent = node;
    }

    return node;
}

/* AST Tree Manipulation Functions */
bool ast_add_child(ast_node_t *parent, ast_node_t *child)
{
    if (!parent || !child) {
        error_set(ERROR_INVALID_ARGUMENT, "Parent and child cannot be null");
        return false;
    }

    child->parent = parent;

    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        /* Find the last child and append */
        ast_node_t *current = parent->first_child;
        while (current->next_sibling) {
            current = current->next_sibling;
        }
        current->next_sibling = child;
    }

    return true;
}

bool ast_remove_child(ast_node_t *parent, ast_node_t *child)
{
    if (!parent || !child) {
        error_set(ERROR_INVALID_ARGUMENT, "Parent and child cannot be null");
        return false;
    }

    if (parent->first_child == child) {
        parent->first_child = child->next_sibling;
        child->parent = NULL;
        child->next_sibling = NULL;
        return true;
    }

    ast_node_t *current = parent->first_child;
    while (current && current->next_sibling != child) {
        current = current->next_sibling;
    }

    if (current) {
        current->next_sibling = child->next_sibling;
        child->parent = NULL;
        child->next_sibling = NULL;
        return true;
    }

    return false;
}

ast_node_t *ast_get_child(const ast_node_t *parent, size_t index)
{
    if (!parent) {
        return NULL;
    }

    ast_node_t *current = parent->first_child;
    size_t i = 0;

    while (current && i < index) {
        current = current->next_sibling;
        i++;
    }

    return current;
}

size_t ast_get_child_count(const ast_node_t *parent)
{
    if (!parent) {
        return 0;
    }

    size_t count = 0;
    ast_node_t *current = parent->first_child;

    while (current) {
        count++;
        current = current->next_sibling;
    }

    return count;
}

/* AST Traversal Functions */
void ast_traverse_preorder(ast_node_t *root, ast_visitor_func_t visitor, void *user_data)
{
    if (!root || !visitor) {
        return;
    }

    /* Visit current node */
    if (!visitor(root, user_data)) {
        return;  /* Visitor returned false, stop traversal */
    }

    /* Visit children based on node type */
    switch (root->type) {
        case AST_DECISION:
            if (root->data.decision.true_branch) {
                ast_traverse_preorder(root->data.decision.true_branch, visitor, user_data);
            }
            if (root->data.decision.false_branch) {
                ast_traverse_preorder(root->data.decision.false_branch, visitor, user_data);
            }
            break;

        case AST_RULE:
            if (root->data.rule.body) {
                ast_traverse_preorder(root->data.rule.body, visitor, user_data);
            }
            break;

        case AST_LOGIC_OP:
            if (root->data.logic_op.left) {
                ast_traverse_preorder(root->data.logic_op.left, visitor, user_data);
            }
            if (root->data.logic_op.right) {
                ast_traverse_preorder(root->data.logic_op.right, visitor, user_data);
            }
            break;

        case AST_COMPARISON:
            if (root->data.comparison.left) {
                ast_traverse_preorder(root->data.comparison.left, visitor, user_data);
            }
            if (root->data.comparison.right) {
                ast_traverse_preorder(root->data.comparison.right, visitor, user_data);
            }
            break;

        case AST_CHAIN:
            if (root->data.chain.first) {
                ast_traverse_preorder(root->data.chain.first, visitor, user_data);
            }
            if (root->data.chain.second) {
                ast_traverse_preorder(root->data.chain.second, visitor, user_data);
            }
            break;

        case AST_CONSEQUENCE:
        case AST_IDENTIFIER:
        case AST_LITERAL:
            /* Leaf nodes - no children to traverse */
            break;
    }

    /* Also traverse generic children */
    ast_node_t *child = root->first_child;
    while (child) {
        ast_traverse_preorder(child, visitor, user_data);
        child = child->next_sibling;
    }
}

void ast_traverse_postorder(ast_node_t *root, ast_visitor_func_t visitor, void *user_data)
{
    if (!root || !visitor) {
        return;
    }

    /* Visit children first based on node type */
    switch (root->type) {
        case AST_DECISION:
            if (root->data.decision.true_branch) {
                ast_traverse_postorder(root->data.decision.true_branch, visitor, user_data);
            }
            if (root->data.decision.false_branch) {
                ast_traverse_postorder(root->data.decision.false_branch, visitor, user_data);
            }
            break;

        case AST_RULE:
            if (root->data.rule.body) {
                ast_traverse_postorder(root->data.rule.body, visitor, user_data);
            }
            break;

        case AST_LOGIC_OP:
            if (root->data.logic_op.left) {
                ast_traverse_postorder(root->data.logic_op.left, visitor, user_data);
            }
            if (root->data.logic_op.right) {
                ast_traverse_postorder(root->data.logic_op.right, visitor, user_data);
            }
            break;

        case AST_COMPARISON:
            if (root->data.comparison.left) {
                ast_traverse_postorder(root->data.comparison.left, visitor, user_data);
            }
            if (root->data.comparison.right) {
                ast_traverse_postorder(root->data.comparison.right, visitor, user_data);
            }
            break;

        case AST_CHAIN:
            if (root->data.chain.first) {
                ast_traverse_postorder(root->data.chain.first, visitor, user_data);
            }
            if (root->data.chain.second) {
                ast_traverse_postorder(root->data.chain.second, visitor, user_data);
            }
            break;

        case AST_CONSEQUENCE:
        case AST_IDENTIFIER:
        case AST_LITERAL:
            /* Leaf nodes - no children to traverse */
            break;
    }

    /* Also traverse generic children */
    ast_node_t *child = root->first_child;
    while (child) {
        ast_traverse_postorder(child, visitor, user_data);
        child = child->next_sibling;
    }

    /* Visit current node after children */
    visitor(root, user_data);
}

ast_node_t *ast_find_node(ast_node_t *root, ast_predicate_func_t predicate, void *user_data)
{
    if (!root || !predicate) {
        return NULL;
    }

    if (predicate(root, user_data)) {
        return root;
    }

    /* Search in children based on node type */
    switch (root->type) {
        case AST_DECISION: {
            ast_node_t *found = NULL;
            if (root->data.decision.true_branch) {
                found = ast_find_node(root->data.decision.true_branch, predicate, user_data);
                if (found) return found;
            }
            if (root->data.decision.false_branch) {
                found = ast_find_node(root->data.decision.false_branch, predicate, user_data);
                if (found) return found;
            }
            break;
        }

        case AST_RULE:
            if (root->data.rule.body) {
                return ast_find_node(root->data.rule.body, predicate, user_data);
            }
            break;

        case AST_LOGIC_OP: {
            ast_node_t *found = NULL;
            if (root->data.logic_op.left) {
                found = ast_find_node(root->data.logic_op.left, predicate, user_data);
                if (found) return found;
            }
            if (root->data.logic_op.right) {
                found = ast_find_node(root->data.logic_op.right, predicate, user_data);
                if (found) return found;
            }
            break;
        }

        case AST_COMPARISON: {
            ast_node_t *found = NULL;
            if (root->data.comparison.left) {
                found = ast_find_node(root->data.comparison.left, predicate, user_data);
                if (found) return found;
            }
            if (root->data.comparison.right) {
                found = ast_find_node(root->data.comparison.right, predicate, user_data);
                if (found) return found;
            }
            break;
        }

        case AST_CHAIN: {
            ast_node_t *found = NULL;
            if (root->data.chain.first) {
                found = ast_find_node(root->data.chain.first, predicate, user_data);
                if (found) return found;
            }
            if (root->data.chain.second) {
                found = ast_find_node(root->data.chain.second, predicate, user_data);
                if (found) return found;
            }
            break;
        }

        case AST_CONSEQUENCE:
        case AST_IDENTIFIER:
        case AST_LITERAL:
            /* Leaf nodes - already checked above */
            break;
    }

    /* Search in generic children */
    ast_node_t *child = root->first_child;
    while (child) {
        ast_node_t *found = ast_find_node(child, predicate, user_data);
        if (found) {
            return found;
        }
        child = child->next_sibling;
    }

    return NULL;
}

void ast_clear_tree(ast_node_t **root)
{
    if (!root || !*root) {
        return;
    }

    ast_destroy(*root);
    *root = NULL;
}

/* AST Cloning Functions */
ast_node_t *ast_clone(const ast_node_t *node)
{
    if (!node) {
        return NULL;
    }

    ast_node_t *clone = ast_create_node(node->type);
    if (!clone) {
        return NULL;
    }

    /* Copy source location information */
    clone->line = node->line;
    clone->column = node->column;

    /* Clone type-specific data */
    switch (node->type) {
        case AST_DECISION:
            clone->data.decision.condition = string_duplicate(node->data.decision.condition);
            if (!clone->data.decision.condition) {
                ast_destroy(clone);
                return NULL;
            }
            clone->data.decision.condition_type = node->data.decision.condition_type;
            clone->data.decision.priority = node->data.decision.priority;
            
            clone->data.decision.true_branch = ast_clone(node->data.decision.true_branch);
            clone->data.decision.false_branch = ast_clone(node->data.decision.false_branch);
            
            if (clone->data.decision.true_branch) {
                clone->data.decision.true_branch->parent = clone;
            }
            if (clone->data.decision.false_branch) {
                clone->data.decision.false_branch->parent = clone;
            }
            break;

        case AST_CONSEQUENCE:
            clone->data.consequence.action = string_duplicate(node->data.consequence.action);
            if (!clone->data.consequence.action) {
                ast_destroy(clone);
                return NULL;
            }
            clone->data.consequence.type = node->data.consequence.type;
            clone->data.consequence.weight = node->data.consequence.weight;
            clone->data.consequence.executed = node->data.consequence.executed;
            break;

        case AST_RULE:
            clone->data.rule.name = string_duplicate(node->data.rule.name);
            if (!clone->data.rule.name) {
                ast_destroy(clone);
                return NULL;
            }
            clone->data.rule.is_active = node->data.rule.is_active;
            clone->data.rule.execution_count = node->data.rule.execution_count;
            
            clone->data.rule.body = ast_clone(node->data.rule.body);
            if (clone->data.rule.body) {
                clone->data.rule.body->parent = clone;
            }
            break;

        case AST_LOGIC_OP:
            clone->data.logic_op.op = node->data.logic_op.op;
            clone->data.logic_op.left = ast_clone(node->data.logic_op.left);
            clone->data.logic_op.right = ast_clone(node->data.logic_op.right);
            
            if (clone->data.logic_op.left) {
                clone->data.logic_op.left->parent = clone;
            }
            if (clone->data.logic_op.right) {
                clone->data.logic_op.right->parent = clone;
            }
            break;

        case AST_COMPARISON:
            clone->data.comparison.op = node->data.comparison.op;
            clone->data.comparison.left = ast_clone(node->data.comparison.left);
            clone->data.comparison.right = ast_clone(node->data.comparison.right);
            
            if (clone->data.comparison.left) {
                clone->data.comparison.left->parent = clone;
            }
            if (clone->data.comparison.right) {
                clone->data.comparison.right->parent = clone;
            }
            break;

        case AST_IDENTIFIER:
            clone->data.identifier.name = string_duplicate(node->data.identifier.name);
            if (!clone->data.identifier.name) {
                ast_destroy(clone);
                return NULL;
            }
            break;

        case AST_LITERAL:
            clone->data.literal.value = node->data.literal.value;
            if (node->data.literal.value.type == VALUE_STRING && 
                node->data.literal.value.data.string_val) {
                clone->data.literal.value.data.string_val = 
                    string_duplicate(node->data.literal.value.data.string_val);
                if (!clone->data.literal.value.data.string_val) {
                    ast_destroy(clone);
                    return NULL;
                }
            }
            break;

        case AST_CHAIN:
            clone->data.chain.chain_type = node->data.chain.chain_type;
            clone->data.chain.first = ast_clone(node->data.chain.first);
            clone->data.chain.second = ast_clone(node->data.chain.second);
            
            if (clone->data.chain.first) {
                clone->data.chain.first->parent = clone;
            }
            if (clone->data.chain.second) {
                clone->data.chain.second->parent = clone;
            }
            break;
    }

    /* Clone generic children */
    const ast_node_t *child = node->first_child;
    ast_node_t *last_cloned_child = NULL;

    while (child) {
        ast_node_t *cloned_child = ast_clone(child);
        if (!cloned_child) {
            ast_destroy(clone);
            return NULL;
        }

        cloned_child->parent = clone;

        if (!clone->first_child) {
            clone->first_child = cloned_child;
        } else {
            last_cloned_child->next_sibling = cloned_child;
        }

        last_cloned_child = cloned_child;
        child = child->next_sibling;
    }

    return clone;
}

/* AST Comparison Functions */
bool ast_equals(const ast_node_t *a, const ast_node_t *b)
{
    if (a == b) {
        return true;  /* Same pointer or both NULL */
    }

    if (!a || !b) {
        return false;  /* One is NULL, the other is not */
    }

    if (a->type != b->type) {
        return false;
    }

    /* Compare type-specific data */
    switch (a->type) {
        case AST_DECISION:
            if (strcmp(a->data.decision.condition, b->data.decision.condition) != 0 ||
                a->data.decision.condition_type != b->data.decision.condition_type ||
                a->data.decision.priority != b->data.decision.priority) {
                return false;
            }
            return ast_equals(a->data.decision.true_branch, b->data.decision.true_branch) &&
                   ast_equals(a->data.decision.false_branch, b->data.decision.false_branch);

        case AST_CONSEQUENCE:
            return strcmp(a->data.consequence.action, b->data.consequence.action) == 0 &&
                   a->data.consequence.type == b->data.consequence.type &&
                   a->data.consequence.weight == b->data.consequence.weight;

        case AST_RULE:
            if (strcmp(a->data.rule.name, b->data.rule.name) != 0 ||
                a->data.rule.is_active != b->data.rule.is_active) {
                return false;
            }
            return ast_equals(a->data.rule.body, b->data.rule.body);

        case AST_LOGIC_OP:
            if (a->data.logic_op.op != b->data.logic_op.op) {
                return false;
            }
            return ast_equals(a->data.logic_op.left, b->data.logic_op.left) &&
                   ast_equals(a->data.logic_op.right, b->data.logic_op.right);

        case AST_COMPARISON:
            if (a->data.comparison.op != b->data.comparison.op) {
                return false;
            }
            return ast_equals(a->data.comparison.left, b->data.comparison.left) &&
                   ast_equals(a->data.comparison.right, b->data.comparison.right);

        case AST_IDENTIFIER:
            return strcmp(a->data.identifier.name, b->data.identifier.name) == 0;

        case AST_LITERAL:
            return reasons_value_equals(&a->data.literal.value, &b->data.literal.value);

        case AST_CHAIN:
            if (a->data.chain.chain_type != b->data.chain.chain_type) {
                return false;
            }
            return ast_equals(a->data.chain.first, b->data.chain.first) &&
                   ast_equals(a->data.chain.second, b->data.chain.second);
    }

    /* Compare generic children */
    const ast_node_t *child_a = a->first_child;
    const ast_node_t *child_b = b->first_child;

    while (child_a && child_b) {
        if (!ast_equals(child_a, child_b)) {
            return false;
        }
        child_a = child_a->next_sibling;
        child_b = child_b->next_sibling;
    }

    /* Both should have reached the end at the same time */
    return child_a == NULL && child_b == NULL;
}

/* AST String Representation Functions */
static void ast_print_indent(FILE *fp, int level)
{
    for (int i = 0; i < level * 2; i++) {
        fputc(' ', fp);
    }
}

static void ast_print_node_recursive(const ast_node_t *node, FILE *fp, int level)
{
    if (!node) {
        ast_print_indent(fp, level);
        fprintf(fp, "(null)\n");
        return;
    }

    ast_print_indent(fp, level);

    switch (node->type) {
        case AST_DECISION:
            fprintf(fp, "Decision: \"%s\" (type=%d, priority=%d)\n", 
                    node->data.decision.condition,
                    node->data.decision.condition_type,
                    node->data.decision.priority);
            if (node->data.decision.true_branch) {
                ast_print_indent(fp, level + 1);
                fprintf(fp, "True:\n");
                ast_print_node_recursive(node->data.decision.true_branch, fp, level + 2);
            }
            if (node->data.decision.false_branch) {
                ast_print_indent(fp, level + 1);
                fprintf(fp, "False:\n");
                ast_print_node_recursive(node->data.decision.false_branch, fp, level + 2);
            }
            break;

        case AST_CONSEQUENCE:
            fprintf(fp, "Consequence: \"%s\" (type=%d, weight=%.2f, executed=%s)\n",
                    node->data.consequence.action,
                    node->data.consequence.type,
                    node->data.consequence.weight,
                    node->data.consequence.executed ? "yes" : "no");
            break;

        case AST_RULE:
            fprintf(fp, "Rule: \"%s\" (active=%s, count=%zu)\n",
                    node->data.rule.name,
                    node->data.rule.is_active ? "yes" : "no",
                    node->data.rule.execution_count);
            if (node->data.rule.body) {
                ast_print_node_recursive(node->data.rule.body, fp, level + 1);
            }
            break;

        case AST_LOGIC_OP:
            fprintf(fp, "LogicOp: %s\n", 
                    node->data.logic_op.op == LOGIC_AND ? "AND" :
                    node->data.logic_op.op == LOGIC_OR ? "OR" :
                    node->data.logic_op.op == LOGIC_NOT ? "NOT" : "UNKNOWN");
            if (node->data.logic_op.left) {
                ast_print_indent(fp, level + 1);
                fprintf(fp, "Left:\n");
                ast_print_node_recursive(node->data.logic_op.left, fp, level + 2);
            }
            if (node->data.logic_op.right) {
                ast_print_indent(fp, level + 1);
                fprintf(fp, "Right:\n");
                ast_print_node_recursive(node->data.logic_op.right, fp, level + 2);
            }
            break;

        case AST_COMPARISON:
            fprintf(fp, "Comparison: %s\n",
                    node->data.comparison.op == CMP_EQ ? "==" :
                    node->data.comparison.op == CMP_NE ? "!=" :
                    node->data.comparison.op == CMP_LT ? "<" :
                    node->data.comparison.op == CMP_LE ? "<=" :
                    node->data.comparison.op == CMP_GT ? ">" :
                    node->data.comparison.op == CMP_GE ? ">=" : "UNKNOWN");
            if (node->data.comparison.left) {
                ast_print_indent(fp, level + 1);
                fprintf(fp, "Left:\n");
                ast_print_node_recursive(node->data.comparison.left, fp, level + 2);
            }
            if (node->data.comparison.right) {
                ast_print_indent(fp, level + 1);
                fprintf(fp, "Right:\n");
                ast_print_node_recursive(node->data.comparison.right, fp, level + 2);
            }
            break;

        case AST_IDENTIFIER:
            fprintf(fp, "Identifier: \"%s\"\n", node->data.identifier.name);
            break;

        case AST_LITERAL:
            fprintf(fp, "Literal: ");
            reasons_value_print(&node->data.literal.value, fp);
            fprintf(fp, "\n");
            break;

        case AST_CHAIN:
            fprintf(fp, "Chain: %s\n",
                    node->data.chain.chain_type == CHAIN_SEQUENTIAL ? "sequential" :
                    node->data.chain.chain_type == CHAIN_PARALLEL ? "parallel" : "unknown");
            if (node->data.chain.first) {
                ast_print_indent(fp, level + 1);
                fprintf(fp, "First:\n");
                ast_print_node_recursive(node->data.chain.first, fp, level + 2);
            }
            if (node->data.chain.second) {
                ast_print_indent(fp, level + 1);
                fprintf(fp, "Second:\n");
                ast_print_node_recursive(node->data.chain.second, fp, level + 2);
            }
            break;

        default:
            fprintf(fp, "Unknown node type: %d\n", node->type);
            break;
    }

    /* Print generic children */
    const ast_node_t *child = node->first_child;
    if (child) {
        ast_print_indent(fp, level + 1);
        fprintf(fp, "Children:\n");
        while (child) {
            ast_print_node_recursive(child, fp, level + 2);
            child = child->next_sibling;
        }
    }
}

void ast_print(const ast_node_t *root, FILE *fp)
{
    if (!fp) {
        fp = stdout;
    }

    if (!root) {
        fprintf(fp, "AST: (empty)\n");
        return;
    }

    fprintf(fp, "AST:\n");
    ast_print_node_recursive(root, fp, 1);
}

void ast_print_compact(const ast_node_t *node, FILE *fp)
{
    if (!fp) {
        fp = stdout;
    }

    if (!node) {
        fprintf(fp, "()");
        return;
    }

    switch (node->type) {
        case AST_DECISION:
            fprintf(fp, "(%s ? ", node->data.decision.condition);
            ast_print_compact(node->data.decision.true_branch, fp);
            fprintf(fp, " : ");
            ast_print_compact(node->data.decision.false_branch, fp);
            fprintf(fp, ")");
            break;

        case AST_CONSEQUENCE:
            fprintf(fp, "[%s]", node->data.consequence.action);
            break;

        case AST_RULE:
            fprintf(fp, "rule %s { ", node->data.rule.name);
            ast_print_compact(node->data.rule.body, fp);
            fprintf(fp, " }");
            break;

        case AST_LOGIC_OP:
            fprintf(fp, "(");
            if (node->data.logic_op.op == LOGIC_NOT) {
                fprintf(fp, "!");
                ast_print_compact(node->data.logic_op.left, fp);
            } else {
                ast_print_compact(node->data.logic_op.left, fp);
                fprintf(fp, " %s ", 
                        node->data.logic_op.op == LOGIC_AND ? "&&" : "||");
                ast_print_compact(node->data.logic_op.right, fp);
            }
            fprintf(fp, ")");
            break;

        case AST_COMPARISON:
            fprintf(fp, "(");
            ast_print_compact(node->data.comparison.left, fp);
            fprintf(fp, " %s ",
                    node->data.comparison.op == CMP_EQ ? "==" :
                    node->data.comparison.op == CMP_NE ? "!=" :
                    node->data.comparison.op == CMP_LT ? "<" :
                    node->data.comparison.op == CMP_LE ? "<=" :
                    node->data.comparison.op == CMP_GT ? ">" :
                    node->data.comparison.op == CMP_GE ? ">=" : "?");
            ast_print_compact(node->data.comparison.right, fp);
            fprintf(fp, ")");
            break;

        case AST_IDENTIFIER:
            fprintf(fp, "%s", node->data.identifier.name);
            break;

        case AST_LITERAL:
            reasons_value_print(&node->data.literal.value, fp);
            break;

        case AST_CHAIN:
            ast_print_compact(node->data.chain.first, fp);
            fprintf(fp, " %s ", 
                    node->data.chain.chain_type == CHAIN_SEQUENTIAL ? ";" : "||");
            ast_print_compact(node->data.chain.second, fp);
            break;

        default:
            fprintf(fp, "<?>");
            break;
    }
}

/* AST Statistics and Debugging */
void ast_get_statistics(ast_statistics_t *stats)
{
    if (!stats) {
        return;
    }

    stats->total_nodes = g_ast_stats.total_nodes;
    stats->max_depth = g_ast_stats.max_depth;
    stats->current_depth = g_ast_stats.current_depth;
    stats->decisions_created = g_ast_stats.decisions_created;
    stats->consequences_created = g_ast_stats.consequences_created;
    stats->rules_created = g_ast_stats.rules_created;
}

void ast_reset_statistics(void)
{
    memset(&g_ast_stats, 0, sizeof(g_ast_stats));
}

const char *ast_node_type_name(ast_node_type_t type)
{
    switch (type) {
        case AST_DECISION: return "Decision";
        case AST_CONSEQUENCE: return "Consequence";
        case AST_RULE: return "Rule";
        case AST_LOGIC_OP: return "LogicOp";
        case AST_COMPARISON: return "Comparison";
        case AST_IDENTIFIER: return "Identifier";
        case AST_LITERAL: return "Literal";
        case AST_CHAIN: return "Chain";
        default: return "Unknown";
    }
}

/* Golf-specific AST optimizations */
ast_node_t *ast_create_golf_shorthand(const char *expr)
{
    if (!expr) {
        return NULL;
    }

    /* Handle common golf patterns:
     * "x>5?win:lose" -> decision with consequences
     * "x&y" -> logic AND
     * "x|y" -> logic OR
     * "!x" -> logic NOT
     */

    /* Simple ternary operator parsing */
    char *question = strchr(expr, '?');
    char *colon = strchr(expr, ':');
    
    if (question && colon && question < colon) {
        /* Extract condition, true, false parts */
        size_t cond_len = question - expr;
        size_t true_len = colon - question - 1;
        size_t false_len = strlen(expr) - (colon - expr) - 1;
        
        char *condition = memory_allocate(cond_len + 1);
        char *true_part = memory_allocate(true_len + 1);
        char *false_part = memory_allocate(false_len + 1);
        
        if (!condition || !true_part || !false_part) {
            memory_free(condition);
            memory_free(true_part);
            memory_free(false_part);
            return NULL;
        }
        
        strncpy(condition, expr, cond_len);
        condition[cond_len] = '\0';
        
        strncpy(true_part, question + 1, true_len);
        true_part[true_len] = '\0';
        
        strncpy(false_part, colon + 1, false_len);
        false_part[false_len] = '\0';
        
        /* Create consequence nodes for branches */
        ast_node_t *true_branch = ast_create_consequence(true_part, CONSEQUENCE_ACTION);
        ast_node_t *false_branch = ast_create_consequence(false_part, CONSEQUENCE_ACTION);
        
        ast_node_t *decision = ast_create_decision(condition, true_branch, false_branch);
        
        memory_free(condition);
        memory_free(true_part);
        memory_free(false_part);
        
        return decision;
    }
    
    /* If no ternary pattern, create a simple identifier */
    return ast_create_identifier(expr);
}

ast_node_t *ast_optimize_for_golf(ast_node_t *root)
{
    if (!root) {
        return NULL;
    }
    
    /* Optimization passes for golf:
     * 1. Collapse trivial decisions
     * 2. Merge adjacent consequences
     * 3. Simplify logic expressions
     */
    
    switch (root->type) {
        case AST_DECISION:
            /* If condition is always true/false literal, collapse */
            if (root->data.decision.true_branch && 
                root->data.decision.true_branch->type == AST_LITERAL) {
                const reasons_value_t *val = &root->data.decision.true_branch->data.literal.value;
                if (val->type == VALUE_BOOL && val->data.bool_val) {
                    /* Always true - return true branch */
                    ast_node_t *result = ast_clone(root->data.decision.true_branch);
                    return result;
                }
            }
            break;
            
        case AST_LOGIC_OP:
            /* Optimize logic operations */
            if (root->data.logic_op.op == LOGIC_AND) {
                /* If either operand is false literal, return false */
                /* If either operand is true literal, return the other */
            }
            break;
            
        default:
            break;
    }
    
    return ast_clone(root);
}

/* Utility function for creating common decision patterns */
ast_node_t *ast_create_if_then_else(const char *condition, 
                                   const char *then_action, 
                                   const char *else_action)
{
    ast_node_t *then_conseq = ast_create_consequence(then_action, CONSEQUENCE_ACTION);
    ast_node_t *else_conseq = else_action ? 
        ast_create_consequence(else_action, CONSEQUENCE_ACTION) : NULL;
    
    return ast_create_decision(condition, then_conseq, else_conseq);
}

ast_node_t *ast_create_simple_rule(const char *rule_name, 
                                  const char *condition,
                                  const char *action)
{
    ast_node_t *consequence = ast_create_consequence(action, CONSEQUENCE_ACTION);
    ast_node_t *decision = ast_create_decision(condition, consequence, NULL);
    
    return ast_create_rule(rule_name, decision);
}

/* AST Utility Functions */
size_t ast_get_depth(const ast_node_t *root)
{
    if (!root) {
        return 0;
    }

    return ast_calculate_depth(root);
}

static size_t ast_calculate_depth(const ast_node_t *node)
{
    if (!node) {
        return 0;
    }

    size_t max_child_depth = 0;
    size_t depth;

    /* Check type-specific children */
    switch (node->type) {
        case AST_DECISION:
            depth = ast_calculate_depth(node->data.decision.true_branch);
            if (depth > max_child_depth) max_child_depth = depth;
            
            depth = ast_calculate_depth(node->data.decision.false_branch);
            if (depth > max_child_depth) max_child_depth = depth;
            break;

        case AST_RULE:
            depth = ast_calculate_depth(node->data.rule.body);
            if (depth > max_child_depth) max_child_depth = depth;
            break;

        case AST_LOGIC_OP:
            depth = ast_calculate_depth(node->data.logic_op.left);
            if (depth > max_child_depth) max_child_depth = depth;
            
            depth = ast_calculate_depth(node->data.logic_op.right);
            if (depth > max_child_depth) max_child_depth = depth;
            break;

        case AST_COMPARISON:
            depth = ast_calculate_depth(node->data.comparison.left);
            if (depth > max_child_depth) max_child_depth = depth;
            
            depth = ast_calculate_depth(node->data.comparison.right);
            if (depth > max_child_depth) max_child_depth = depth;
            break;

        case AST_CHAIN:
            depth = ast_calculate_depth(node->data.chain.first);
            if (depth > max_child_depth) max_child_depth = depth;
            
            depth = ast_calculate_depth(node->data.chain.second);
            if (depth > max_child_depth) max_child_depth = depth;
            break;

        case AST_CONSEQUENCE:
        case AST_IDENTIFIER:
        case AST_LITERAL:
            /* Leaf nodes */
            break;
    }

    /* Check generic children */
    const ast_node_t *child = node->first_child;
    while (child) {
        depth = ast_calculate_depth(child);
        if (depth > max_child_depth) {
            max_child_depth = depth;
        }
        child = child->next_sibling;
    }

    return max_child_depth + 1;
}

size_t ast_count_nodes(const ast_node_t *root)
{
    if (!root) {
        return 0;
    }

    size_t count = 1;  /* Count this node */

    /* Count type-specific children */
    switch (root->type) {
        case AST_DECISION:
            if (root->data.decision.true_branch) {
                count += ast_count_nodes(root->data.decision.true_branch);
            }
            if (root->data.decision.false_branch) {
                count += ast_count_nodes(root->data.decision.false_branch);
            }
            break;

        case AST_RULE:
            if (root->data.rule.body) {
                count += ast_count_nodes(root->data.rule.body);
            }
            break;

        case AST_LOGIC_OP:
            if (root->data.logic_op.left) {
                count += ast_count_nodes(root->data.logic_op.left);
            }
            if (root->data.logic_op.right) {
                count += ast_count_nodes(root->data.logic_op.right);
            }
            break;

        case AST_COMPARISON:
            if (root->data.comparison.left) {
                count += ast_count_nodes(root->data.comparison.left);
            }
            if (root->data.comparison.right) {
                count += ast_count_nodes(root->data.comparison.right);
            }
            break;

        case AST_CHAIN:
            if (root->data.chain.first) {
                count += ast_count_nodes(root->data.chain.first);
            }
            if (root->data.chain.second) {
                count += ast_count_nodes(root->data.chain.second);
            }
            break;

        case AST_CONSEQUENCE:
        case AST_IDENTIFIER:
        case AST_LITERAL:
            /* Leaf nodes - already counted */
            break;
    }

    /* Count generic children */
    const ast_node_t *child = root->first_child;
    while (child) {
        count += ast_count_nodes(child);
        child = child->next_sibling;
    }

    return count;
}

bool ast_validate(const ast_node_t *root)
{
    if (!root) {
        return true;  /* Empty tree is valid */
    }

    return ast_validate_node_recursive(root, 0);
}

static bool ast_validate_node_recursive(const ast_node_t *node, int depth)
{
    if (!node) {
        return true;
    }

    if (depth > AST_MAX_DEPTH) {
        error_set(ERROR_INVALID_STATE, "AST exceeds maximum depth");
        return false;
    }

    /* Validate node type */
    if (node->type < 0 || node->type >= AST_NODE_TYPE_COUNT) {
        error_set(ERROR_INVALID_STATE, "Invalid AST node type");
        return false;
    }

    /* Validate type-specific data */
    switch (node->type) {
        case AST_DECISION:
            if (!node->data.decision.condition) {
                error_set(ERROR_INVALID_STATE, "Decision node missing condition");
                return false;
            }
            /* Validate branches */
            if (node->data.decision.true_branch && 
                !ast_validate_node_recursive(node->data.decision.true_branch, depth + 1)) {
                return false;
            }
            if (node->data.decision.false_branch && 
                !ast_validate_node_recursive(node->data.decision.false_branch, depth + 1)) {
                return false;
            }
            break;

        case AST_CONSEQUENCE:
            if (!node->data.consequence.action) {
                error_set(ERROR_INVALID_STATE, "Consequence node missing action");
                return false;
            }
            if (node->data.consequence.type < 0 || 
                node->data.consequence.type >= CONSEQUENCE_TYPE_COUNT) {
                error_set(ERROR_INVALID_STATE, "Invalid consequence type");
                return false;
            }
            break;

        case AST_RULE:
            if (!node->data.rule.name) {
                error_set(ERROR_INVALID_STATE, "Rule node missing name");
                return false;
            }
            if (node->data.rule.body && 
                !ast_validate_node_recursive(node->data.rule.body, depth + 1)) {
                return false;
            }
            break;

        case AST_LOGIC_OP:
            if (node->data.logic_op.op < 0 || 
                node->data.logic_op.op >= LOGIC_OP_COUNT) {
                error_set(ERROR_INVALID_STATE, "Invalid logic operation");
                return false;
            }
            /* Validate operands */
            if (node->data.logic_op.left && 
                !ast_validate_node_recursive(node->data.logic_op.left, depth + 1)) {
                return false;
            }
            if (node->data.logic_op.right && 
                !ast_validate_node_recursive(node->data.logic_op.right, depth + 1)) {
                return false;
            }
            break;

        case AST_COMPARISON:
            if (node->data.comparison.op < 0 || 
                node->data.comparison.op >= COMPARISON_OP_COUNT) {
                error_set(ERROR_INVALID_STATE, "Invalid comparison operation");
                return false;
            }
            /* Validate operands */
            if (node->data.comparison.left && 
                !ast_validate_node_recursive(node->data.comparison.left, depth + 1)) {
                return false;
            }
            if (node->data.comparison.right && 
                !ast_validate_node_recursive(node->data.comparison.right, depth + 1)) {
                return false;
            }
            break;

        case AST_IDENTIFIER:
            if (!node->data.identifier.name) {
                error_set(ERROR_INVALID_STATE, "Identifier node missing name");
                return false;
            }
            break;

        case AST_LITERAL:
            /* Literals are always valid if they exist */
            break;

        case AST_CHAIN:
            if (node->data.chain.first && 
                !ast_validate_node_recursive(node->data.chain.first, depth + 1)) {
                return false;
            }
            if (node->data.chain.second && 
                !ast_validate_node_recursive(node->data.chain.second, depth + 1)) {
                return false;
            }
            break;
    }

    /* Validate generic children */
    const ast_node_t *child = node->first_child;
    while (child) {
        if (!ast_validate_node_recursive(child, depth + 1)) {
            return false;
        }
        /* Validate parent relationship */
        if (child->parent != node) {
            error_set(ERROR_INVALID_STATE, "Child node has incorrect parent pointer");
            return false;
        }
        child = child->next_sibling;
    }

    return true;
}

/* AST Cleanup Functions */
void ast_destroy(ast_node_t *root)
{
    if (!root) {
        return;
    }

    ast_node_destroy_recursive(root);
    g_ast_stats.total_nodes--;

    LOG_DEBUG("Destroyed AST node, total nodes now: %zu", g_ast_stats.total_nodes);
}

static void ast_node_destroy_recursive(ast_node_t *node)
{
    if (!node) {
        return;
    }

    /* Destroy type-specific data and children */
    switch (node->type) {
        case AST_DECISION:
            memory_free(node->data.decision.condition);
            ast_node_destroy_recursive(node->data.decision.true_branch);
            ast_node_destroy_recursive(node->data.decision.false_branch);
            break;

        case AST_CONSEQUENCE:
            memory_free(node->data.consequence.action);
            break;

        case AST_RULE:
            memory_free(node->data.rule.name);
            ast_node_destroy_recursive(node->data.rule.body);
            break;

        case AST_LOGIC_OP:
            ast_node_destroy_recursive(node->data.logic_op.left);
            ast_node_destroy_recursive(node->data.logic_op.right);
            break;

        case AST_COMPARISON:
            ast_node_destroy_recursive(node->data.comparison.left);
            ast_node_destroy_recursive(node->data.comparison.right);
            break;

        case AST_IDENTIFIER:
            memory_free(node->data.identifier.name);
            break;

        case AST_LITERAL:
            if (node->data.literal.value.type == VALUE_STRING) {
                memory_free(node->data.literal.value.data.string_val);
            }
            break;

        case AST_CHAIN:
            ast_node_destroy_recursive(node->data.chain.first);
            ast_node_destroy_recursive(node->data.chain.second);
            break;
    }

    /* Destroy generic children */
    ast_node_t *child = node->first_child;
    while (child) {
        ast_node_t *next = child->next_sibling;
        ast_node_destroy_recursive(child);
        child = next;
    }

    /* Free the node itself */
    memory_free(node);
}
