/*
 * parser.c - Recursive Descent Parser for Reasons DSL
 * 
 * Implements a predictive parser with error recovery and golf syntax support.
 * Uses a recursive descent approach with Pratt parsing for expressions.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reasons/parser.h"
#include "reasons/lexer.h"
#include "reasons/ast.h"
#include "reasons/types.h"
#include "utils/error.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/string_utils.h"

/* Parser state structure */
struct parser_state {
    lexer_t *lexer;             /* Lexer instance */
    token_t current_token;      /* Current token being processed */
    token_t previous_token;     /* Previous consumed token */
    bool had_error;             /* True if any error occurred */
    bool panic_mode;            /* Error recovery flag */
    bool in_condition_context;  /* Context-aware parsing */
    bool in_consequence_context;
    ast_node_t *current_rule;   /* Current rule being parsed */
    size_t recursion_depth;     /* Prevent stack overflow */
};

/* Operator precedence levels */
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_TERNARY,     // ?:
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * / %
    PREC_UNARY,       // ! - +
    PREC_CALL,        // . () []
    PREC_PRIMARY
} precedence_t;

/* Forward declarations */
static ast_node_t *parse_expression(parser_t *parser, precedence_t precedence);
static void parser_synchronize(parser_t *parser);
static void parser_advance(parser_t *parser);
static void parser_error_at(parser_t *parser, token_t *token, const char *message);
static void parser_error_current(parser_t *parser, const char *message);
static bool parser_match(parser_t *parser, token_type_t type);
static bool parser_check(parser_t *parser, token_type_t type);
static bool parser_consume(parser_t *parser, token_type_t type, const char *message);
static precedence_t get_precedence(token_type_t type);

/* Parser creation/destruction */
parser_t *parser_create(lexer_t *lexer)
{
    if (!lexer) {
        error_set(ERROR_INVALID_ARGUMENT, "Lexer cannot be null");
        return NULL;
    }

    parser_t *parser = memory_allocate(sizeof(parser_t));
    if (!parser) {
        error_set(ERROR_MEMORY, "Failed to allocate parser");
        return NULL;
    }

    memset(parser, 0, sizeof(parser_t));
    parser->lexer = lexer;
    parser->had_error = false;
    parser->panic_mode = false;
    parser->in_condition_context = false;
    parser->in_consequence_context = false;
    parser->current_rule = NULL;
    parser->recursion_depth = 0;

    /* Load first token */
    parser_advance(parser);

    LOG_DEBUG("Parser created successfully");
    return parser;
}

void parser_destroy(parser_t *parser)
{
    if (!parser) return;
    
    token_free(&parser->current_token);
    token_free(&parser->previous_token);
    memory_free(parser);
    
    LOG_DEBUG("Parser destroyed");
}

/* Core parsing functions */
ast_node_t *parser_parse(parser_t *parser)
{
    if (!parser) return NULL;

    ast_node_t *program = ast_create_node(AST_PROGRAM);
    if (!program) return NULL;

    while (!lexer_at_end(parser->lexer) {
        if (parser->had_error) break;

        ast_node_t *declaration = parse_rule_declaration(parser);
        if (declaration) {
            ast_add_child(program, declaration);
        } else {
            /* Try to parse as a top-level decision */
            declaration = parse_decision(parser);
            if (declaration) {
                ast_add_child(program, declaration);
            } else {
                /* Skip to next declaration */
                parser_synchronize(parser);
            }
        }
    }

    if (parser->had_error) {
        ast_destroy(program);
        return NULL;
    }

    return program;
}

static ast_node_t *parse_rule_declaration(parser_t *parser)
{
    if (!parser_match(parser, TOKEN_RULE)) {
        return NULL;
    }

    token_t name_token = parser->previous_token;
    if (!parser_consume(parser, TOKEN_IDENTIFIER, "Expected rule name")) {
        return NULL;
    }

    /* Create rule node */
    ast_node_t *rule = ast_create_rule(name_token.value, NULL);
    if (!rule) return NULL;
    parser->current_rule = rule;

    /* Parse rule body */
    if (!parser_consume(parser, TOKEN_LBRACE, "Expected '{' before rule body")) {
        ast_destroy(rule);
        return NULL;
    }

    while (!parser_check(parser, TOKEN_RBRACE) && !parser->had_error) {
        ast_node_t *stmt = parse_statement(parser);
        if (stmt) {
            ast_add_child(rule->data.rule.body, stmt);
        } else {
            parser_synchronize(parser);
        }
    }

    if (!parser_consume(parser, TOKEN_RBRACE, "Expected '}' after rule body")) {
        ast_destroy(rule);
        return NULL;
    }

    parser->current_rule = NULL;
    return rule;
}

static ast_node_t *parse_statement(parser_t *parser)
{
    if (parser_match(parser, TOKEN_IF)) {
        return parse_decision(parser);
    } else if (parser_match(parser, TOKEN_RULE)) {
        return parse_rule_declaration(parser);
    } else if (parser_match(parser, TOKEN_WHEN)) {
        return parse_when_statement(parser);
    } else {
        return parse_consequence(parser);
    }
}

static ast_node_t *parse_decision(parser_t *parser)
{
    /* Save current context and set condition context */
    bool prev_condition = parser->in_condition_context;
    parser->in_condition_context = true;
    
    /* Parse condition */
    ast_node_t *condition = parse_expression(parser, PREC_NONE);
    if (!condition) {
        parser->in_condition_context = prev_condition;
        return NULL;
    }
    
    /* Parse 'then' branch */
    if (!parser_consume(parser, TOKEN_THEN, "Expected 'then' after condition")) {
        ast_destroy(condition);
        parser->in_condition_context = prev_condition;
        return NULL;
    }
    
    /* Parse true consequence */
    bool prev_consequence = parser->in_consequence_context;
    parser->in_consequence_context = true;
    ast_node_t *true_branch = parse_consequence(parser);
    parser->in_consequence_context = prev_consequence;
    
    if (!true_branch) {
        ast_destroy(condition);
        parser->in_condition_context = prev_condition;
        return NULL;
    }
    
    /* Parse optional 'else' branch */
    ast_node_t *false_branch = NULL;
    if (parser_match(parser, TOKEN_ELSE)) {
        parser->in_consequence_context = true;
        false_branch = parse_consequence(parser);
        parser->in_consequence_context = prev_consequence;
        
        if (!false_branch) {
            ast_destroy(condition);
            ast_destroy(true_branch);
            parser->in_condition_context = prev_condition;
            return NULL;
        }
    }
    
    /* Parse 'end' keyword */
    if (!parser_consume(parser, TOKEN_END, "Expected 'end' after decision")) {
        ast_destroy(condition);
        ast_destroy(true_branch);
        ast_destroy(false_branch);
        parser->in_condition_context = prev_condition;
        return NULL;
    }
    
    /* Restore context */
    parser->in_condition_context = prev_condition;
    
    /* Create decision node */
    ast_node_t *decision = ast_create_decision("<condition>", true_branch, false_branch);
    if (!decision) {
        ast_destroy(condition);
        ast_destroy(true_branch);
        ast_destroy(false_branch);
        return NULL;
    }
    
    /* Attach condition expression */
    ast_add_child(decision, condition);
    return decision;
}

static ast_node_t *parse_when_statement(parser_t *parser)
{
    token_t condition = parser->current_token;
    if (!parser_consume(parser, TOKEN_IDENTIFIER, "Expected condition identifier")) {
        return NULL;
    }
    
    if (!parser_consume(parser, TOKEN_DO, "Expected 'do' after condition")) {
        return NULL;
    }
    
    ast_node_t *consequence = parse_consequence(parser);
    if (!consequence) return NULL;
    
    return ast_create_decision(condition.value, consequence, NULL);
}

static ast_node_t *parse_consequence(parser_t *parser)
{
    /* Try golf shorthand consequences */
    if (parser->in_consequence_context) {
        if (parser_match(parser, TOKEN_WIN) ||
            parser_match(parser, TOKEN_LOSE) ||
            parser_match(parser, TOKEN_DRAW) ||
            parser_match(parser, TOKEN_SKIP) ||
            parser_match(parser, TOKEN_PASS) ||
            parser_match(parser, TOKEN_FAIL)) {
            
            consequence_type_t type = 
                (parser->previous_token.type == TOKEN_WIN) ? CONSEQUENCE_WIN :
                (parser->previous_token.type == TOKEN_LOSE) ? CONSEQUENCE_LOSE :
                (parser->previous_token.type == TOKEN_DRAW) ? CONSEQUENCE_DRAW :
                (parser->previous_token.type == TOKEN_SKIP) ? CONSEQUENCE_SKIP :
                (parser->previous_token.type == TOKEN_PASS) ? CONSEQUENCE_PASS : CONSEQUENCE_FAIL;
            
            return ast_create_consequence(parser->previous_token.value, type);
        }
    }
    
    /* Regular consequence */
    ast_node_t *expr = parse_expression(parser, PREC_ASSIGNMENT);
    if (!expr) return NULL;
    
    /* If we have a simple identifier, promote to consequence */
    if (expr->type == AST_IDENTIFIER) {
        ast_node_t *conseq = ast_create_consequence(expr->data.identifier.name, CONSEQUENCE_ACTION);
        ast_destroy(expr);
        return conseq;
    }
    
    return expr;
}

/* Expression parsing */
static ast_node_t *parse_expression(parser_t *parser, precedence_t precedence)
{
    if (parser->recursion_depth > MAX_RECURSION_DEPTH) {
        parser_error_current(parser, "Expression too complex");
        return NULL;
    }
    
    parser->recursion_depth++;
    
    /* Parse prefix expression */
    ast_node_t *left;
    switch (parser->current_token.type) {
        case TOKEN_TRUE:
        case TOKEN_FALSE: {
            token_t token = parser->current_token;
            parser_advance(parser);
            
            reasons_value_t value;
            value.type = VALUE_BOOL;
            value.data.bool_val = (token.type == TOKEN_TRUE);
            left = ast_create_literal(&value);
            break;
        }
        
        case TOKEN_NUMBER: {
            token_t token = parser->current_token;
            parser_advance(parser);
            
            reasons_value_t value;
            value.type = VALUE_NUMBER;
            value.data.number_val = atof(token.value);
            left = ast_create_literal(&value);
            break;
        }
        
        case TOKEN_STRING: {
            token_t token = parser->current_token;
            parser_advance(parser);
            
            reasons_value_t value;
            value.type = VALUE_STRING;
            value.data.string_val = token.value;
            left = ast_create_literal(&value);
            break;
        }
        
        case TOKEN_IDENTIFIER: {
            token_t token = parser->current_token;
            parser_advance(parser);
            left = ast_create_identifier(token.value);
            break;
        }
        
        case TOKEN_LPAREN: {
            parser_advance(parser);
            left = parse_expression(parser, PREC_NONE);
            if (!parser_consume(parser, TOKEN_RPAREN, "Expected ')' after expression")) {
                ast_destroy(left);
                left = NULL;
            }
            break;
        }
        
        case TOKEN_LOGICAL_NOT:
        case TOKEN_MINUS: {
            token_t op_token = parser->current_token;
            parser_advance(parser);
            
            ast_node_t *right = parse_expression(parser, PREC_UNARY);
            if (!right) {
                left = NULL;
                break;
            }
            
            logic_op_t op = (op_token.type == TOKEN_LOGICAL_NOT) ? LOGIC_NOT : LOGIC_NEGATE;
            left = ast_create_logic_op(op, right, NULL);
            break;
        }
        
        default:
            parser_error_current(parser, "Expected expression");
            left = NULL;
            break;
    }
    
    /* Parse infix expressions */
    while (left && precedence <= get_precedence(parser->current_token.type)) {
        token_t operator = parser->current_token;
        parser_advance(parser);
        
        switch (operator.type) {
            case TOKEN_LOGICAL_AND:
            case TOKEN_LOGICAL_OR: {
                logic_op_t op = (operator.type == TOKEN_LOGICAL_AND) ? LOGIC_AND : LOGIC_OR;
                ast_node_t *right = parse_expression(parser, get_precedence(operator.type) + 1);
                left = ast_create_logic_op(op, left, right);
                break;
            }
            
            case TOKEN_EQUAL:
            case TOKEN_NOT_EQUAL:
            case TOKEN_LESS:
            case TOKEN_LESS_EQUAL:
            case TOKEN_GREATER:
            case TOKEN_GREATER_EQUAL: {
                comparison_op_t op = 
                    (operator.type == TOKEN_EQUAL) ? CMP_EQ :
                    (operator.type == TOKEN_NOT_EQUAL) ? CMP_NE :
                    (operator.type == TOKEN_LESS) ? CMP_LT :
                    (operator.type == TOKEN_LESS_EQUAL) ? CMP_LE :
                    (operator.type == TOKEN_GREATER) ? CMP_GT : CMP_GE;
                
                ast_node_t *right = parse_expression(parser, get_precedence(operator.type) + 1);
                left = ast_create_comparison(op, left, right);
                break;
            }
            
            case TOKEN_PLUS:
            case TOKEN_MINUS:
            case TOKEN_MULTIPLY:
            case TOKEN_DIVIDE: {
                /* Handle arithmetic operators */
                ast_node_t *right = parse_expression(parser, get_precedence(operator.type) + 1);
                /* Create appropriate AST node (implementation specific) */
                break;
            }
            
            case TOKEN_QUESTION: {
                /* Ternary operator */
                ast_node_t *true_expr = parse_expression(parser, PREC_NONE);
                if (!parser_consume(parser, TOKEN_COLON, "Expected ':' in ternary expression")) {
                    ast_destroy(left);
                    ast_destroy(true_expr);
                    left = NULL;
                    break;
                }
                
                ast_node_t *false_expr = parse_expression(parser, PREC_TERNARY);
                left = ast_create_decision("<ternary>", true_expr, false_expr);
                ast_add_child(left, left);  /* Original condition becomes child */
                break;
            }
            
            default:
                /* Shouldn't reach here */
                break;
        }
    }
    
    parser->recursion_depth--;
    return left;
}

/* Token management */
static void parser_advance(parser_t *parser)
{
    if (!parser) return;
    
    token_free(&parser->previous_token);
    parser->previous_token = parser->current_token;
    
    if (parser->panic_mode) {
        /* Skip tokens until synchronization point */
        while (!lexer_at_end(parser->lexer)) {
            token_t token = lexer_next_token(parser->lexer);
            if (token.type == TOKEN_SEMICOLON || 
                token.type == TOKEN_END ||
                token.type == TOKEN_RBRACE) {
                parser->panic_mode = false;
                parser->current_token = token;
                return;
            }
            token_free(&token);
        }
    }
    
    parser->current_token = lexer_next_token(parser->lexer);
    
    /* Handle newlines as statement terminators in golf mode */
    if (parser->current_token.type == TOKEN_NEWLINE && 
        parser->lexer->options.golf_mode) {
        parser_advance(parser);
    }
}

static bool parser_match(parser_t *parser, token_type_t type)
{
    if (!parser || parser->had_error) return false;
    
    if (parser_check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    return false;
}

static bool parser_check(parser_t *parser, token_type_t type)
{
    if (lexer_at_end(parser->lexer)) return false;
    return parser->current_token.type == type;
}

static bool parser_consume(parser_t *parser, token_type_t type, const char *message)
{
    if (parser_check(parser, type)) {
        parser_advance(parser);
        return true;
    }
    
    parser_error_current(parser, message);
    return false;
}

/* Error handling */
static void parser_error_at(parser_t *parser, token_t *token, const char *message)
{
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    parser->had_error = true;
    
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "[Line %zu, Col %zu] Parser error: %s",
             token->line, token->column, message);
    
    LOG_ERROR("%s", error_msg);
    error_set(ERROR_SYNTAX, error_msg);
}

static void parser_error_current(parser_t *parser, const char *message)
{
    parser_error_at(parser, &parser->current_token, message);
}

static void parser_synchronize(parser_t *parser)
{
    parser->panic_mode = true;
    
    while (!lexer_at_end(parser->lexer)) {
        if (parser->previous_token.type == TOKEN_SEMICOLON ||
            parser->previous_token.type == TOKEN_END) {
            return;
        }
        
        switch (parser->current_token.type) {
            case TOKEN_RULE:
            case TOKEN_IF:
            case TOKEN_WHEN:
            case TOKEN_END:
            case TOKEN_RBRACE:
                return;
            default:
                break;
        }
        
        parser_advance(parser);
    }
}

/* Operator precedence */
static precedence_t get_precedence(token_type_t type)
{
    switch (type) {
        case TOKEN_ASSIGN:       return PREC_ASSIGNMENT;
        case TOKEN_QUESTION:     return PREC_TERNARY;
        case TOKEN_LOGICAL_OR:   return PREC_OR;
        case TOKEN_LOGICAL_AND:  return PREC_AND;
        case TOKEN_EQUAL:        
        case TOKEN_NOT_EQUAL:    return PREC_EQUALITY;
        case TOKEN_LESS:         
        case TOKEN_LESS_EQUAL:   
        case TOKEN_GREATER:      
        case TOKEN_GREATER_EQUAL: return PREC_COMPARISON;
        case TOKEN_PLUS:         
        case TOKEN_MINUS:        return PREC_TERM;
        case TOKEN_MULTIPLY:     
        case TOKEN_DIVIDE:       
        case TOKEN_MODULO:       return PREC_FACTOR;
        case TOKEN_LOGICAL_NOT:  
        case TOKEN_MINUS:        return PREC_UNARY;
        case TOKEN_LPAREN:       return PREC_CALL;
        default:                 return PREC_NONE;
    }
}

/* Context-aware parsing helpers */
void parser_set_condition_context(parser_t *parser, bool enabled)
{
    if (parser) parser->in_condition_context = enabled;
}

void parser_set_consequence_context(parser_t *parser, bool enabled)
{
    if (parser) parser->in_consequence_context = enabled;
}

/* Error checking */
bool parser_had_error(const parser_t *parser)
{
    return parser && parser->had_error;
}
