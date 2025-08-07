#ifndef REASONS_TYPES_H
#define REASONS_TYPES_H

#include <stdbool.h>
#include <stddef.h>

/* ======== VALUE TYPE DEFINITIONS ======== */

typedef enum {
    VALUE_NULL,     // Null/nil value
    VALUE_BOOL,     // Boolean value
    VALUE_NUMBER,   // Numeric value (double)
    VALUE_STRING,   // String value
    VALUE_LIST,     // List/array
    VALUE_DICT,     // Dictionary/hash map
    VALUE_FUNCTION, // Function reference
    VALUE_OBJECT,   // Custom object
    VALUE_VOID      // No value (void)
} ValueType;

typedef struct reasons_value {
    ValueType type;
    union {
        bool bool_val;
        double number_val;
        char *string_val;
        struct vector *list_val;
        struct hash_table *dict_val;
        struct {
            char *name;
            unsigned arity;
        } function_val;
        void *object_val;
    } data;
} reasons_value_t;

/* ======== AST NODE DEFINITIONS ======== */

typedef enum {
    AST_LITERAL,        // Literal value
    AST_VARIABLE,       // Variable reference
    AST_BINARY_OP,      // Binary operation
    AST_UNARY_OP,       // Unary operation
    AST_FUNCTION_CALL,  // Function call
    AST_CONDITIONAL,    // Conditional (ternary) expression
    AST_ASSIGNMENT,     // Variable assignment
    AST_BLOCK,          // Block of expressions
    AST_FUNCTION_DEF,   // Function definition
    AST_IF_STATEMENT,   // If statement
    AST_LOOP,           // Loop structure
    AST_BREAK,          // Break statement
    AST_CONTINUE,       // Continue statement
    AST_RETURN,         // Return statement
    AST_PROPERTY_ACCESS // Object property access
} ASTNodeType;

typedef struct AST_Node {
    ASTNodeType type;
    int line;           // Source line number
    int column;         // Source column number
    struct AST_Node *parent; // Parent node
    
    union {
        reasons_value_t literal;    // For AST_LITERAL
        char *variable_name;        // For AST_VARIABLE
        struct {
            char *op;
            struct AST_Node *left;
            struct AST_Node *right;
        } binary_op;                // For AST_BINARY_OP
        struct {
            char *op;
            struct AST_Node *operand;
        } unary_op;                 // For AST_UNARY_OP
        struct {
            struct AST_Node *function;
            struct vector *arguments;
        } function_call;           // For AST_FUNCTION_CALL
        struct {
            struct AST_Node *condition;
            struct AST_Node *true_branch;
            struct AST_Node *false_branch;
        } conditional;             // For AST_CONDITIONAL
        struct {
            char *variable_name;
            struct AST_Node *value;
        } assignment;              // For AST_ASSIGNMENT
        struct vector *block;      // For AST_BLOCK
        struct {
            char *name;
            struct vector *parameters;
            struct AST_Node *body;
        } function_def;            // For AST_FUNCTION_DEF
        struct {
            struct AST_Node *condition;
            struct AST_Node *if_branch;
            struct AST_Node *else_branch;
        } if_statement;            // For AST_IF_STATEMENT
        struct {
            char *loop_type;      // "while", "for", etc.
            struct AST_Node *init;
            struct AST_Node *condition;
            struct AST_Node *update;
            struct AST_Node *body;
        } loop;                    // For AST_LOOP
        struct {
            char *object;
            char *property;
        } property_access;        // For AST_PROPERTY_ACCESS
    };
} AST_Node;

/* ======== RUNTIME TYPE DEFINITIONS ======== */

typedef struct runtime_env runtime_env_t;
typedef struct eval_context eval_context_t;

// Function signature for runtime functions
typedef reasons_value_t (*runtime_function_t)(
    runtime_env_t *env, 
    const reasons_value_t *args, 
    size_t num_args
);

// Consequence handling
typedef enum {
    CONSEQUENCE_ANY,
    CONSEQUENCE_UPDATE,
    CONSEQUENCE_NOTIFY,
    CONSEQUENCE_LOG,
    CONSEQUENCE_CALCULATE
} consequence_type_t;

typedef struct {
    bool handled;
    bool success;
    reasons_value_t *value;
    char *message;
} consequence_result_t;

typedef consequence_result_t (*consequence_handler_t)(
    runtime_env_t *env, 
    AST_Node *action
);

/* ======== DEBUGGER TYPE DEFINITIONS ======== */

typedef struct DebuggerState {
    runtime_env_t *env;
    eval_context_t *eval_ctx;
    bool active;
    unsigned breakpoint_count;
    // ... other debugger state fields
} DebuggerState;

/* ======== DECISION TREE TYPES ======== */

typedef enum {
    NODE_CONDITION,
    NODE_ACTION,
    NODE_OUTCOME
} NodeType;

typedef struct TreeNode {
    NodeType type;
    char *id;
    char *description;
    int line;
    int column;
    
    // Tree relationships
    struct TreeNode *parent;
    struct TreeNode *true_branch;
    struct TreeNode *false_branch;
    
    // ... other tree node fields
} TreeNode;

typedef struct DecisionTree {
    TreeNode *root;
    char *name;
    // ... other tree fields
} DecisionTree;

/* ======== REPL TYPE DEFINITIONS ======== */

typedef struct REPLState {
    runtime_env_t *env;
    eval_context_t *eval_ctx;
    DebuggerState *debugger;
    bool running;
    bool verbose;
    bool debug_mode;
    unsigned line_count;
    char *last_error;
    char *current_script;
    struct vector *input_buffer;
} REPLState;

/* ======== VISUALIZATION TYPES ======== */

typedef struct GraphContext GraphContext;
typedef struct GraphNode GraphNode;
typedef struct GraphEdge GraphEdge;

/* ======== UTILITY TYPE DEFINITIONS ======== */

// Dynamic array implementation
typedef struct vector {
    void **items;
    size_t capacity;
    size_t size;
} vector_t;

// Hash table implementation
typedef struct hash_table {
    struct hash_bucket **buckets;
    size_t capacity;
    size_t size;
} hash_table_t;

// Error codes
typedef enum {
    ERROR_NONE,
    ERROR_SYNTAX,
    ERROR_TYPE,
    ERROR_UNDEFINED,
    ERROR_RECURSION,
    ERROR_ARGUMENT,
    ERROR_MEMORY,
    ERROR_IO,
    ERROR_RUNTIME,
    ERROR_INTERNAL
} error_t;

#endif /* REASONS_TYPES_H */
