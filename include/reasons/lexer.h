#ifndef REASONS_LEXER_H
#define REASONS_LEXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "reasons/types.h"

/* Lexer configuration constants */
#define LEXER_LOOKAHEAD_SIZE 3
#define LEXER_MAX_ERROR_LENGTH 256

/* Token types */
typedef enum {
    TOKEN_EOF,
    TOKEN_ERROR,
    TOKEN_NEWLINE,
    
    /* Literals */
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_TRUE,
    TOKEN_FALSE,
    
    /* Keywords */
    TOKEN_IF,
    TOKEN_THEN,
    TOKEN_ELSE,
    TOKEN_END,
    TOKEN_RULE,
    TOKEN_WHEN,
    TOKEN_DO,
    TOKEN_WIN,
    TOKEN_LOSE,
    TOKEN_DRAW,
    TOKEN_SKIP,
    TOKEN_FAIL,
    TOKEN_PASS,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_USE,
    TOKEN_MODULE,
    TOKEN_RETURN,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_DEFAULT,
    TOKEN_WILDCARD,
    TOKEN_ANY,
    TOKEN_ALL,
    
    /* Operators */
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MULTIPLY,
    TOKEN_DIVIDE,
    TOKEN_MODULO,
    TOKEN_POWER,
    TOKEN_ASSIGN,
    TOKEN_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_LOGICAL_AND,
    TOKEN_LOGICAL_OR,
    TOKEN_LOGICAL_NOT,
    TOKEN_INCREMENT,
    TOKEN_DECREMENT,
    TOKEN_ARROW,
    TOKEN_IMPLIES,
    TOKEN_CHAIN,
    
    /* Punctuation */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_QUESTION,
    TOKEN_COLON,
    TOKEN_TILDE,
    TOKEN_DOLLAR,
    TOKEN_AT,
    TOKEN_AMPERSAND,
    TOKEN_PIPE,
    
    /* Special */
    TOKEN_SEQUENCE,
    TOKEN_PARALLEL,
    TOKEN_NUMBER_TYPE,
    TOKEN_STRING_TYPE,
    TOKEN_BOOL_TYPE,
    TOKEN_SHIFT_LEFT,
    
    TOKEN_TYPE_COUNT
} token_type_t;

/* Lexer context types */
typedef enum {
    LEXER_CONTEXT_DEFAULT,
    LEXER_CONTEXT_CONDITION,
    LEXER_CONTEXT_CONSEQUENCE,
    LEXER_CONTEXT_RULE_BODY
} lexer_context_t;

/* Lexer options structure */
typedef struct {
    bool skip_whitespace;
    bool skip_comments;
    bool case_sensitive;
    bool golf_mode;
    bool track_positions;
} lexer_options_t;

/* Lexer statistics structure */
typedef struct {
    size_t tokens_produced;
    size_t errors_encountered;
    size_t current_line;
    size_t current_column;
    size_t bytes_processed;
    size_t total_bytes;
} lexer_statistics_t;

/* Token structure */
typedef struct {
    token_type_t type;
    const char *value;   /* Allocated string for token text */
    size_t length;       /* Length of token text */
    size_t line;         /* Line number (1-based) */
    size_t column;       /* Column number (1-based) */
} token_t;

/* Lexer position structure */
typedef struct {
    size_t offset;       /* Character offset in input */
    size_t line;         /* Line number (1-based) */
    size_t column;       /* Column number (1-based) */
} lexer_position_t;

/* Lexer handle (opaque structure) */
typedef struct lexer_state lexer_t;

/* Lexer creation and destruction */
lexer_t *lexer_create(const char *source);
lexer_t *lexer_create_with_options(const char *source, const lexer_options_t *options);
void lexer_destroy(lexer_t *lexer);

/* Token scanning */
token_t lexer_next_token(lexer_t *lexer);
token_t lexer_peek_token(lexer_t *lexer, size_t offset);
bool lexer_match_token(lexer_t *lexer, token_type_t type);
bool lexer_at_end(const lexer_t *lexer);

/* Token utilities */
const char *lexer_token_name(token_type_t type);
void lexer_print_token(const token_t *token, FILE *fp);
void token_free(token_t *token);

/* Position and error handling */
lexer_position_t lexer_get_position(const lexer_t *lexer);
void lexer_set_position(lexer_t *lexer, const lexer_position_t *pos);
void lexer_get_statistics(const lexer_t *lexer, lexer_statistics_t *stats);
bool lexer_has_errors(const lexer_t *lexer);
void lexer_reset_errors(lexer_t *lexer);

/* Golf-specific extensions */
bool lexer_is_golf_operator(const token_t *token);
bool lexer_is_golf_keyword(const token_t *token);
token_t lexer_scan_golf_shorthand(lexer_t *lexer);

/* Context-sensitive tokenization */
void lexer_set_context(lexer_context_t context);
lexer_context_t lexer_get_context(void);
token_t lexer_next_context_token(lexer_t *lexer);

/* Configuration */
void lexer_set_options(lexer_t *lexer, const lexer_options_t *options);
void lexer_get_options(const lexer_t *lexer, lexer_options_t *options);

/* Token stream utilities */
bool lexer_consume_token(lexer_t *lexer, token_type_t expected, token_t *consumed);
bool lexer_expect_token(lexer_t *lexer, token_type_t expected, const char *context);
void lexer_synchronize(lexer_t *lexer);

#endif /* REASONS_LEXER_H */
