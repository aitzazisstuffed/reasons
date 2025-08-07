/*
 * lexer.c - Lexical Analyzer for Reasons DSL
 * 
 * This module implements the lexical analyzer (tokenizer) for the Reasons
 * decision tree language. It's designed for golf-friendly syntax with compact
 * operators and keywords while supporting complex decision logic.
 * 
 * Key features:
 * - Golf-optimized token set (short keywords, compact operators)
 * - Context-sensitive tokenization for decisions and consequences
 * - Support for string literals, numbers, identifiers
 * - Error recovery and detailed error reporting
 * - Lookahead support for parser disambiguation
 * - Comments and whitespace handling
 */

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reasons/lexer.h"
#include "reasons/types.h"
#include "utils/error.h"
#include "utils/memory.h"
#include "utils/logger.h"
#include "utils/string_utils.h"

/* Lexer state structure */
struct lexer_state {
    const char *input;          /* Input source code */
    size_t input_length;        /* Length of input */
    size_t position;            /* Current position in input */
    size_t line;                /* Current line number (1-based) */
    size_t column;              /* Current column number (1-based) */
    size_t token_start;         /* Start position of current token */
    size_t token_line;          /* Line where current token started */
    size_t token_column;        /* Column where current token started */
    bool at_eof;                /* True if at end of file */
    bool error_recovery_mode;   /* True if recovering from error */
    char current_char;          /* Current character being processed */
    
    /* Lookahead buffer for context-sensitive parsing */
    token_t lookahead[LEXER_LOOKAHEAD_SIZE];
    size_t lookahead_count;
    size_t lookahead_pos;
    
    /* Statistics */
    size_t tokens_produced;
    size_t errors_encountered;
    
    /* Options */
    lexer_options_t options;
};

/* Golf-optimized keyword table */
static const struct {
    const char *text;
    token_type_t type;
    size_t length;
} g_keywords[] = {
    /* Core decision keywords - ultra short for golf */
    {"if", TOKEN_IF, 2},
    {"?", TOKEN_QUESTION, 1},      /* Ternary conditional */
    {":", TOKEN_COLON, 1},         /* Ternary else */
    {"then", TOKEN_THEN, 4},
    {"else", TOKEN_ELSE, 4},
    {"end", TOKEN_END, 3},
    
    /* Rule keywords */
    {"rule", TOKEN_RULE, 4},
    {"when", TOKEN_WHEN, 4},
    {"do", TOKEN_DO, 2},
    
    /* Consequence keywords */
    {"win", TOKEN_WIN, 3},         /* Golf: positive outcome */
    {"lose", TOKEN_LOSE, 4},       /* Golf: negative outcome */
    {"draw", TOKEN_DRAW, 4},       /* Golf: neutral outcome */
    {"skip", TOKEN_SKIP, 4},       /* Golf: no-op consequence */
    {"fail", TOKEN_FAIL, 4},       /* Golf: failure consequence */
    {"pass", TOKEN_PASS, 4},       /* Golf: success consequence */
    
    /* Logic operators - single chars for golf */
    {"and", TOKEN_AND, 3},
    {"or", TOKEN_OR, 2},
    {"not", TOKEN_NOT, 3},
    {"&&", TOKEN_LOGICAL_AND, 2},
    {"||", TOKEN_LOGICAL_OR, 2},
    {"!", TOKEN_LOGICAL_NOT, 1},
    
    /* Boolean literals */
    {"true", TOKEN_TRUE, 4},
    {"false", TOKEN_FALSE, 5},
    {"T", TOKEN_TRUE, 1},          /* Golf: short true */
    {"F", TOKEN_FALSE, 1},         /* Golf: short false */
    
    /* Chain operators */
    {"seq", TOKEN_SEQUENCE, 3},    /* Sequential execution */
    {"par", TOKEN_PARALLEL, 3},    /* Parallel execution */
    {">>", TOKEN_CHAIN, 2},        /* Chain operator */
    
    /* Import/module keywords */
    {"use", TOKEN_USE, 3},
    {"mod", TOKEN_MODULE, 3},
    
    /* Type keywords */
    {"num", TOKEN_NUMBER_TYPE, 3},
    {"str", TOKEN_STRING_TYPE, 3},
    {"bool", TOKEN_BOOL_TYPE, 4},
    
    /* Control flow */
    {"ret", TOKEN_RETURN, 3},      /* Golf: return */
    {"brk", TOKEN_BREAK, 3},       /* Golf: break */
    {"cont", TOKEN_CONTINUE, 4},   /* Golf: continue */
    
    /* Special golf constructs */
    {"_", TOKEN_WILDCARD, 1},      /* Wildcard/don't care */
    {"def", TOKEN_DEFAULT, 3},     /* Default case */
    {"any", TOKEN_ANY, 3},         /* Any match */
    {"all", TOKEN_ALL, 3},         /* All match */
};

#define KEYWORD_COUNT (sizeof(g_keywords) / sizeof(g_keywords[0]))

/* Forward declarations */
static void lexer_advance(lexer_t *lexer);
static char lexer_peek(const lexer_t *lexer, size_t offset);
static void lexer_skip_whitespace(lexer_t *lexer);
static void lexer_skip_comment(lexer_t *lexer);
static token_t lexer_make_token(const lexer_t *lexer, token_type_t type);
static token_t lexer_make_string_token(const lexer_t *lexer, const char *value, size_t length);
static token_t lexer_make_error_token(const lexer_t *lexer, const char *message);
static token_t lexer_scan_string(lexer_t *lexer);
static token_t lexer_scan_number(lexer_t *lexer);
static token_t lexer_scan_identifier(lexer_t *lexer);
static token_type_t lexer_identify_keyword(const char *text, size_t length);
static bool lexer_is_alpha(char c);
static bool lexer_is_digit(char c);
static bool lexer_is_alnum(char c);
static bool lexer_match_char(lexer_t *lexer, char expected);
static void lexer_mark_token_start(lexer_t *lexer);

/* Lexer initialization and cleanup */

lexer_t *lexer_create(const char *source)
{
    if (!source) {
        error_set(ERROR_INVALID_ARGUMENT, "Source code cannot be null");
        return NULL;
    }

    lexer_t *lexer = memory_allocate(sizeof(lexer_t));
    if (!lexer) {
        error_set(ERROR_MEMORY, "Failed to allocate lexer");
        return NULL;
    }

    memset(lexer, 0, sizeof(lexer_t));
    
    lexer->input = source;
    lexer->input_length = strlen(source);
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->at_eof = (lexer->input_length == 0);
    lexer->current_char = lexer->at_eof ? '\0' : lexer->input[0];
    
    /* Set default options */
    lexer->options.skip_whitespace = true;
    lexer->options.skip_comments = true;
    lexer->options.case_sensitive = true;
    lexer->options.golf_mode = true;
    lexer->options.track_positions = true;
    
    LOG_DEBUG("Created lexer for source of length %zu", lexer->input_length);
    return lexer;
}

lexer_t *lexer_create_with_options(const char *source, const lexer_options_t *options)
{
    lexer_t *lexer = lexer_create(source);
    if (!lexer) {
        return NULL;
    }
    
    if (options) {
        lexer->options = *options;
    }
    
    return lexer;
}

void lexer_destroy(lexer_t *lexer)
{
    if (!lexer) {
        return;
    }
    
    LOG_DEBUG("Destroying lexer (produced %zu tokens, %zu errors)", 
              lexer->tokens_produced, lexer->errors_encountered);
    
    memory_free(lexer);
}

/* Token scanning functions */

token_t lexer_next_token(lexer_t *lexer)
{
    if (!lexer) {
        return (token_t){.type = TOKEN_ERROR, .line = 0, .column = 0};
    }
    
    /* Check lookahead buffer first */
    if (lexer->lookahead_count > 0) {
        token_t token = lexer->lookahead[lexer->lookahead_pos];
        lexer->lookahead_pos = (lexer->lookahead_pos + 1) % LEXER_LOOKAHEAD_SIZE;
        lexer->lookahead_count--;
        return token;
    }
    
    /* Skip whitespace and comments if enabled */
    while (!lexer->at_eof) {
        if (lexer->options.skip_whitespace && isspace(lexer->current_char)) {
            lexer_skip_whitespace(lexer);
            continue;
        }
        
        if (lexer->options.skip_comments && 
            (lexer->current_char == '/' || lexer->current_char == '#')) {
            char next = lexer_peek(lexer, 1);
            if ((lexer->current_char == '/' && next == '/') ||
                (lexer->current_char == '/' && next == '*') ||
                lexer->current_char == '#') {
                lexer_skip_comment(lexer);
                continue;
            }
        }
        
        break;
    }
    
    if (lexer->at_eof) {
        lexer->tokens_produced++;
        return lexer_make_token(lexer, TOKEN_EOF);
    }
    
    lexer_mark_token_start(lexer);
    char c = lexer->current_char;
    lexer_advance(lexer);
    
    token_t token;
    
    switch (c) {
        /* Single-character tokens */
        case '(': token = lexer_make_token(lexer, TOKEN_LPAREN); break;
        case ')': token = lexer_make_token(lexer, TOKEN_RPAREN); break;
        case '[': token = lexer_make_token(lexer, TOKEN_LBRACKET); break;
        case ']': token = lexer_make_token(lexer, TOKEN_RBRACKET); break;
        case '{': token = lexer_make_token(lexer, TOKEN_LBRACE); break;
        case '}': token = lexer_make_token(lexer, TOKEN_RBRACE); break;
        case ',': token = lexer_make_token(lexer, TOKEN_COMMA); break;
        case ';': token = lexer_make_token(lexer, TOKEN_SEMICOLON); break;
        case '?': token = lexer_make_token(lexer, TOKEN_QUESTION); break;
        case ':': token = lexer_make_token(lexer, TOKEN_COLON); break;
        case '~': token = lexer_make_token(lexer, TOKEN_TILDE); break;
        case '$': token = lexer_make_token(lexer, TOKEN_DOLLAR); break;
        case '@': token = lexer_make_token(lexer, TOKEN_AT); break;
        
        /* Arithmetic operators */
        case '+':
            token = lexer_match_char(lexer, '+') ? 
                lexer_make_token(lexer, TOKEN_INCREMENT) :
                lexer_make_token(lexer, TOKEN_PLUS);
            break;
        case '-':
            if (lexer_match_char(lexer, '-')) {
                token = lexer_make_token(lexer, TOKEN_DECREMENT);
            } else if (lexer_match_char(lexer, '>')) {
                token = lexer_make_token(lexer, TOKEN_ARROW);
            } else {
                token = lexer_make_token(lexer, TOKEN_MINUS);
            }
            break;
        case '*': token = lexer_make_token(lexer, TOKEN_MULTIPLY); break;
        case '/': token = lexer_make_token(lexer, TOKEN_DIVIDE); break;
        case '%': token = lexer_make_token(lexer, TOKEN_MODULO); break;
        case '^': token = lexer_make_token(lexer, TOKEN_POWER); break;
        
        /* Assignment operators */
        case '=':
            if (lexer_match_char(lexer, '=')) {
                token = lexer_make_token(lexer, TOKEN_EQUAL);
            } else if (lexer_match_char(lexer, '>')) {
                token = lexer_make_token(lexer, TOKEN_IMPLIES);  /* => for implications */
            } else {
                token = lexer_make_token(lexer, TOKEN_ASSIGN);
            }
            break;
            
        /* Comparison operators */
        case '<':
            if (lexer_match_char(lexer, '=')) {
                token = lexer_make_token(lexer, TOKEN_LESS_EQUAL);
            } else if (lexer_match_char(lexer, '<')) {
                token = lexer_make_token(lexer, TOKEN_SHIFT_LEFT);
            } else {
                token = lexer_make_token(lexer, TOKEN_LESS);
            }
            break;
        case '>':
            if (lexer_match_char(lexer, '=')) {
                token = lexer_make_token(lexer, TOKEN_GREATER_EQUAL);
            } else if (lexer_match_char(lexer, '>')) {
                token = lexer_make_token(lexer, TOKEN_CHAIN);  /* >> for chaining */
            } else {
                token = lexer_make_token(lexer, TOKEN_GREATER);
            }
            break;
        case '!':
            token = lexer_match_char(lexer, '=') ? 
                lexer_make_token(lexer, TOKEN_NOT_EQUAL) :
                lexer_make_token(lexer, TOKEN_LOGICAL_NOT);
            break;
            
        /* Logical operators */
        case '&':
            token = lexer_match_char(lexer, '&') ? 
                lexer_make_token(lexer, TOKEN_LOGICAL_AND) :
                lexer_make_token(lexer, TOKEN_AMPERSAND);
            break;
        case '|':
            token = lexer_match_char(lexer, '|') ? 
                lexer_make_token(lexer, TOKEN_LOGICAL_OR) :
                lexer_make_token(lexer, TOKEN_PIPE);
            break;
            
        /* String literals */
        case '"':
        case '\'':
            /* Backtrack to include quote in scan */
            lexer->position--;
            lexer->column--;
            lexer->current_char = c;
            token = lexer_scan_string(lexer);
            break;
            
        /* Numbers */
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            /* Backtrack to include digit in scan */
            lexer->position--;
            lexer->column--;
            lexer->current_char = c;
            token = lexer_scan_number(lexer);
            break;
            
        /* Identifiers and keywords */
        default:
            if (lexer_is_alpha(c) || c == '_') {
                /* Backtrack to include character in scan */
                lexer->position--;
                lexer->column--;
                lexer->current_char = c;
                token = lexer_scan_identifier(lexer);
            } else {
                /* Unknown character */
                char error_msg[64];
                snprintf(error_msg, sizeof(error_msg), 
                         "Unexpected character '%c' (0x%02x)", c, (unsigned char)c);
                token = lexer_make_error_token(lexer, error_msg);
                lexer->errors_encountered++;
            }
            break;
    }
    
    lexer->tokens_produced++;
    
    LOG_DEBUG("Produced token: type=%d, line=%zu, column=%zu", 
              token.type, token.line, token.column);
    
    return token;
}

token_t lexer_peek_token(lexer_t *lexer, size_t offset)
{
    if (!lexer) {
        return (token_t){.type = TOKEN_ERROR, .line = 0, .column = 0};
    }
    
    /* Fill lookahead buffer as needed */
    while (lexer->lookahead_count <= offset) {
        if (lexer->lookahead_count >= LEXER_LOOKAHEAD_SIZE) {
            /* Buffer full - can't peek further */
            return (token_t){.type = TOKEN_ERROR, .line = 0, .column = 0};
        }
        
        /* Save current state */
        size_t saved_pos = lexer->position;
        size_t saved_line = lexer->line;
        size_t saved_column = lexer->column;
        char saved_char = lexer->current_char;
        bool saved_eof = lexer->at_eof;
        
        /* Get next token */
        token_t token = lexer_next_token(lexer);
        
        /* Add to lookahead buffer */
        size_t buf_idx = (lexer->lookahead_pos + lexer->lookahead_count) % LEXER_LOOKAHEAD_SIZE;
        lexer->lookahead[buf_idx] = token;
        lexer->lookahead_count++;
        
        /* If we hit EOF or error, stop */
        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) {
            break;
        }
    }
    
    /* Return requested token from lookahead buffer */
    if (offset < lexer->lookahead_count) {
        size_t buf_idx = (lexer->lookahead_pos + offset) % LEXER_LOOKAHEAD_SIZE;
        return lexer->lookahead[buf_idx];
    }
    
    return (token_t){.type = TOKEN_ERROR, .line = 0, .column = 0};
}

bool lexer_match_token(lexer_t *lexer, token_type_t type)
{
    if (!lexer) {
        return false;
    }
    
    token_t token = lexer_peek_token(lexer, 0);
    if (token.type == type) {
        lexer_next_token(lexer);  /* Consume the token */
        return true;
    }
    
    return false;
}

bool lexer_at_end(const lexer_t *lexer)
{
    if (!lexer) {
        return true;
    }
    
    /* Check if we have lookahead tokens */
    if (lexer->lookahead_count > 0) {
        size_t idx = (lexer->lookahead_pos + lexer->lookahead_count - 1) % LEXER_LOOKAHEAD_SIZE;
        return lexer->lookahead[idx].type == TOKEN_EOF;
    }
    
    return lexer->at_eof;
}

/* Token utility functions */

const char *lexer_token_name(token_type_t type)
{
    switch (type) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_ERROR: return "ERROR";
        case TOKEN_NEWLINE: return "NEWLINE";
        
        /* Literals */
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_NUMBER: return "NUMBER";
        case TOKEN_STRING: return "STRING";
        case TOKEN_TRUE: return "TRUE";
        case TOKEN_FALSE: return "FALSE";
        
        /* Keywords */
        case TOKEN_IF: return "IF";
        case TOKEN_THEN: return "THEN";
        case TOKEN_ELSE: return "ELSE";
        case TOKEN_END: return "END";
        case TOKEN_RULE: return "RULE";
        case TOKEN_WHEN: return "WHEN";
        case TOKEN_DO: return "DO";
        case TOKEN_WIN: return "WIN";
        case TOKEN_LOSE: return "LOSE";
        case TOKEN_DRAW: return "DRAW";
        case TOKEN_SKIP: return "SKIP";
        case TOKEN_FAIL: return "FAIL";
        case TOKEN_PASS: return "PASS";
        case TOKEN_AND: return "AND";
        case TOKEN_OR: return "OR";
        case TOKEN_NOT: return "NOT";
        case TOKEN_USE: return "USE";
        case TOKEN_MODULE: return "MODULE";
        case TOKEN_RETURN: return "RETURN";
        case TOKEN_BREAK: return "BREAK";
        case TOKEN_CONTINUE: return "CONTINUE";
        case TOKEN_DEFAULT: return "DEFAULT";
        case TOKEN_WILDCARD: return "WILDCARD";
        case TOKEN_ANY: return "ANY";
        case TOKEN_ALL: return "ALL";
        
        /* Operators */
        case TOKEN_PLUS: return "PLUS";
        case TOKEN_MINUS: return "MINUS";
        case TOKEN_MULTIPLY: return "MULTIPLY";
        case TOKEN_DIVIDE: return "DIVIDE";
        case TOKEN_MODULO: return "MODULO";
        case TOKEN_POWER: return "POWER";
        case TOKEN_ASSIGN: return "ASSIGN";
        case TOKEN_EQUAL: return "EQUAL";
        case TOKEN_NOT_EQUAL: return "NOT_EQUAL";
        case TOKEN_LESS: return "LESS";
        case TOKEN_LESS_EQUAL: return "LESS_EQUAL";
        case TOKEN_GREATER: return "GREATER";
        case TOKEN_GREATER_EQUAL: return "GREATER_EQUAL";
        case TOKEN_LOGICAL_AND: return "LOGICAL_AND";
        case TOKEN_LOGICAL_OR: return "LOGICAL_OR";
        case TOKEN_LOGICAL_NOT: return "LOGICAL_NOT";
        case TOKEN_INCREMENT: return "INCREMENT";
        case TOKEN_DECREMENT: return "DECREMENT";
        case TOKEN_ARROW: return "ARROW";
        case TOKEN_IMPLIES: return "IMPLIES";
        case TOKEN_CHAIN: return "CHAIN";
        
        /* Punctuation */
        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_LBRACKET: return "LBRACKET";
        case TOKEN_RBRACKET: return "RBRACKET";
        case TOKEN_LBRACE: return "LBRACE";
        case TOKEN_RBRACE: return "RBRACE";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_QUESTION: return "QUESTION";
        case TOKEN_COLON: return "COLON";
        case TOKEN_TILDE: return "TILDE";
        case TOKEN_DOLLAR: return "DOLLAR";
        case TOKEN_AT: return "AT";
        case TOKEN_AMPERSAND: return "AMPERSAND";
        case TOKEN_PIPE: return "PIPE";
        
        /* Special */
        case TOKEN_SEQUENCE: return "SEQUENCE";
        case TOKEN_PARALLEL: return "PARALLEL";
        case TOKEN_NUMBER_TYPE: return "NUMBER_TYPE";
        case TOKEN_STRING_TYPE: return "STRING_TYPE";
        case TOKEN_BOOL_TYPE: return "BOOL_TYPE";
        case TOKEN_SHIFT_LEFT: return "SHIFT_LEFT";
        
        default: return "UNKNOWN";
    }
}

void lexer_print_token(const token_t *token, FILE *fp)
{
    if (!token || !fp) {
        return;
    }
    
    fprintf(fp, "Token{type=%s", lexer_token_name(token->type));
    
    if (token->value) {
        fprintf(fp, ", value='%s'", token->value);
    }
    
    if (token->length > 0) {
        fprintf(fp, ", length=%zu", token->length);
    }
    
    fprintf(fp, ", line=%zu, column=%zu}", token->line, token->column);
}

void token_free(token_t *token)
{
    if (!token) {
        return;
    }
    
    memory_free((void*)token->value);
    token->value = NULL;
    token->length = 0;
}

/* Lexer state and error handling */

lexer_position_t lexer_get_position(const lexer_t *lexer)
{
    lexer_position_t pos = {0};
    if (lexer) {
        pos.offset = lexer->position;
        pos.line = lexer->line;
        pos.column = lexer->column;
    }
    return pos;
}

void lexer_set_position(lexer_t *lexer, const lexer_position_t *pos)
{
    if (!lexer || !pos) {
        return;
    }
    
    if (pos->offset <= lexer->input_length) {
        lexer->position = pos->offset;
        lexer->line = pos->line;
        lexer->column = pos->column;
        lexer->at_eof = (lexer->position >= lexer->input_length);
        lexer->current_char = lexer->at_eof ? '\0' : lexer->input[lexer->position];
        
        /* Clear lookahead buffer since position changed */
        lexer->lookahead_count = 0;
        lexer->lookahead_pos = 0;
    }
}

void lexer_get_statistics(const lexer_t *lexer, lexer_statistics_t *stats)
{
    if (!lexer || !stats) {
        return;
    }
    
    memset(stats, 0, sizeof(lexer_statistics_t));
    stats->tokens_produced = lexer->tokens_produced;
    stats->errors_encountered = lexer->errors_encountered;
    stats->current_line = lexer->line;
    stats->current_column = lexer->column;
    stats->bytes_processed = lexer->position;
    stats->total_bytes = lexer->input_length;
}

bool lexer_has_errors(const lexer_t *lexer)
{
    return lexer && lexer->errors_encountered > 0;
}

void lexer_reset_errors(lexer_t *lexer)
{
    if (lexer) {
        lexer->errors_encountered = 0;
        lexer->error_recovery_mode = false;
    }
}

/* Internal helper functions */

static void lexer_advance(lexer_t *lexer)
{
    if (!lexer || lexer->at_eof) {
        return;
    }
    
    if (lexer->current_char == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    
    lexer->position++;
    
    if (lexer->position >= lexer->input_length) {
        lexer->at_eof = true;
        lexer->current_char = '\0';
    } else {
        lexer->current_char = lexer->input[lexer->position];
    }
}

static char lexer_peek(const lexer_t *lexer, size_t offset)
{
    if (!lexer) {
        return '\0';
    }
    
    size_t pos = lexer->position + offset;
    if (pos >= lexer->input_length) {
        return '\0';
    }
    
    return lexer->input[pos];
}

static void lexer_skip_whitespace(lexer_t *lexer)
{
    while (!lexer->at_eof && isspace(lexer->current_char)) {
        lexer_advance(lexer);
    }
}

static void lexer_skip_comment(lexer_t *lexer)
{
    if (lexer->current_char == '#') {
        /* Line comment - skip to end of line */
        while (!lexer->at_eof && lexer->current_char != '\n') {
            lexer_advance(lexer);
        }
    } else if (lexer->current_char == '/' && lexer_peek(lexer, 1) == '/') {
        /* Line comment - skip to end of line */
        lexer_advance(lexer);  /* Skip first '/' */
        lexer_advance(lexer);  /* Skip second '/' */
        
        while (!lexer->at_eof && lexer->current_char != '\n') {
            lexer_advance(lexer);
        }
    } else if (lexer->current_char == '/' && lexer_peek(lexer, 1) == '*') {
        /* Block comment - skip to */ */
        lexer_advance(lexer);  /* Skip '/' */
        lexer_advance(lexer);  /* Skip '*' */
        
        while (!lexer->at_eof) {
            if (lexer->current_char == '*' && lexer_peek(lexer, 1) == '/') {
                lexer_advance(lexer);  /* Skip '*' */
                lexer_advance(lexer);  /* Skip '/' */
                break;
            }
            lexer_advance(lexer);
        }
    }
}

static void lexer_mark_token_start(lexer_t *lexer)
{
    lexer->token_start = lexer->position;
    lexer->token_line = lexer->line;
    lexer->token_column = lexer->column;
}

static token_t lexer_make_token(const lexer_t *lexer, token_type_t type)
{
    token_t token;
    memset(&token, 0, sizeof(token));
    
    token.type = type;
    token.line = lexer->token_line;
    token.column = lexer->token_column;
    token.length = lexer->position - lexer->token_start;
    
    if (token.length > 0 && lexer->token_start < lexer->input_length) {
        /* Extract token text */
        char *value = memory_allocate(token.length + 1);
        if (value) {
            memcpy(value, lexer->input + lexer->token_start, token.length);
            value[token.length] = '\0';
            token.value = value;
        }
    }
    
    return token;
}

static token_t lexer_make_string_token(const lexer_t *lexer, const char *value, size_t length)
{
    token_t token;
    memset(&token, 0, sizeof(token));
    
    token.type = TOKEN_STRING;
    token.line = lexer->token_line;
    token.column = lexer->token_column;
    token.length = length;
    
    if (value && length > 0) {
        char *token_value = memory_allocate(length + 1);
        if (token_value) {
            memcpy(token_value, value, length);
            token_value[length] = '\0';
            token.value = token_value;
        }
    }
    
    return token;
}

static token_t lexer_make_error_token(const lexer_t *lexer, const char *message)
{
    token_t token;
    memset(&token, 0, sizeof(token));
    
    token.type = TOKEN_ERROR;
    token.line = lexer->token_line;
    token.column = lexer->token_column;
    
    if (message) {
        size_t msg_len = strlen(message);
        char *error_msg = memory_allocate(msg_len + 1);
        if (error_msg) {
            strcpy(error_msg, message);
            token.value = error_msg;
            token.length = msg_len;
        }
    }
    
    return token;
}

static token_t lexer_scan_string(lexer_t *lexer)
{
    char quote_char = lexer->current_char;
    lexer_advance(lexer);  /* Skip opening quote */
    
    size_t start_pos = lexer->position;
    size_t string_length = 0;
    bool escaped = false;
    
    /* Scan string content */
    while (!lexer->at_eof && (lexer->current_char != quote_char || escaped)) {
        if (escaped) {
            escaped = false;
        } else if (lexer->current_char == '\\') {
            escaped = true;
        }
        
        string_length++;
        lexer_advance(lexer);
    }
    
    if (lexer->at_eof) {
        return lexer_make_error_token(lexer, "Unterminated string literal");
    }
    
    /* Extract string content (without quotes) */
    char *string_value = memory_allocate(string_length + 1);
    if (!string_value) {
        return lexer_make_error_token(lexer, "Out of memory");
    }
    
    /* Process escape sequences */
    const char *src = lexer->input + start_pos;
    char *dst = string_value;
    size_t i = 0;
    
    while (i < string_length) {
        if (src[i] == '\\' && i + 1 < string_length) {
            /* Handle escape sequences */
            switch (src[i + 1]) {
                case 'n': *dst++ = '\n'; break;
                case 't': *dst++ = '\t'; break;
                case 'r': *dst++ = '\r'; break;
                case 'b': *dst++ = '\b'; break;
                case 'f': *dst++ = '\f'; break;
                case 'v': *dst++ = '\v'; break;
                case '\\': *dst++ = '\\'; break;
                case '\'': *dst++ = '\''; break;
                case '\"': *dst++ = '\"'; break;
                case '0': *dst++ = '\0'; break;
                default:
                    /* Unknown escape - keep both characters */
                    *dst++ = src[i];
                    *dst++ = src[i + 1];
                    break;
            }
            i += 2;
        } else {
            *dst++ = src[i];
            i++;
        }
    }
    *dst = '\0';
    
    lexer_advance(lexer);  /* Skip closing quote */
    
    token_t token = lexer_make_string_token(lexer, string_value, dst - string_value);
    memory_free(string_value);
    
    return token;
}

static token_t lexer_scan_number(lexer_t *lexer)
{
    size_t start_pos = lexer->position;
    bool has_dot = false;
    bool has_exp = false;
    bool is_hex = false;
    bool is_binary = false;
    
    /* Check for hex (0x) or binary (0b) prefix */
    if (lexer->current_char == '0') {
        char next = lexer_peek(lexer, 1);
        if (next == 'x' || next == 'X') {
            is_hex = true;
            lexer_advance(lexer);  /* Skip '0' */
            lexer_advance(lexer);  /* Skip 'x' */
        } else if (next == 'b' || next == 'B') {
            is_binary = true;
            lexer_advance(lexer);  /* Skip '0' */
            lexer_advance(lexer);  /* Skip 'b' */
        }
    }
    
    if (is_hex) {
        /* Hexadecimal number */
        while (!lexer->at_eof && 
               (lexer_is_digit(lexer->current_char) ||
                (lexer->current_char >= 'a' && lexer->current_char <= 'f') ||
                (lexer->current_char >= 'A' && lexer->current_char <= 'F'))) {
            lexer_advance(lexer);
        }
    } else if (is_binary) {
        /* Binary number */
        while (!lexer->at_eof && 
               (lexer->current_char == '0' || lexer->current_char == '1')) {
            lexer_advance(lexer);
        }
    } else {
        /* Decimal number */
        while (!lexer->at_eof && lexer_is_digit(lexer->current_char)) {
            lexer_advance(lexer);
        }
        
        /* Check for decimal point */
        if (!lexer->at_eof && lexer->current_char == '.' && 
            lexer_is_digit(lexer_peek(lexer, 1))) {
            has_dot = true;
            lexer_advance(lexer);  /* Skip '.' */
            
            while (!lexer->at_eof && lexer_is_digit(lexer->current_char)) {
                lexer_advance(lexer);
            }
        }
        
        /* Check for scientific notation */
        if (!lexer->at_eof && 
            (lexer->current_char == 'e' || lexer->current_char == 'E')) {
            char next = lexer_peek(lexer, 1);
            if (lexer_is_digit(next) || 
                ((next == '+' || next == '-') && lexer_is_digit(lexer_peek(lexer, 2)))) {
                has_exp = true;
                lexer_advance(lexer);  /* Skip 'e'/'E' */
                
                if (lexer->current_char == '+' || lexer->current_char == '-') {
                    lexer_advance(lexer);  /* Skip sign */
                }
                
                while (!lexer->at_eof && lexer_is_digit(lexer->current_char)) {
                    lexer_advance(lexer);
                }
            }
        }
    }
    
    /* Validate that we have a valid number */
    size_t number_length = lexer->position - start_pos;
    if (number_length == 0 || 
        (is_hex && number_length == 2) ||  /* Just "0x" */
        (is_binary && number_length == 2)) {  /* Just "0b" */
        return lexer_make_error_token(lexer, "Invalid number literal");
    }
    
    return lexer_make_token(lexer, TOKEN_NUMBER);
}

static token_t lexer_scan_identifier(lexer_t *lexer)
{
    size_t start_pos = lexer->position;
    
    /* First character must be alpha or underscore */
    if (!lexer_is_alpha(lexer->current_char) && lexer->current_char != '_') {
        return lexer_make_error_token(lexer, "Invalid identifier");
    }
    
    /* Scan identifier characters */
    while (!lexer->at_eof && lexer_is_alnum(lexer->current_char)) {
        lexer_advance(lexer);
    }
    
    size_t length = lexer->position - start_pos;
    if (length == 0) {
        return lexer_make_error_token(lexer, "Empty identifier");
    }
    
    /* Check if this is a keyword */
    token_type_t keyword_type = lexer_identify_keyword(
        lexer->input + start_pos, length);
    
    if (keyword_type != TOKEN_IDENTIFIER) {
        return lexer_make_token(lexer, keyword_type);
    }
    
    return lexer_make_token(lexer, TOKEN_IDENTIFIER);
}

static token_type_t lexer_identify_keyword(const char *text, size_t length)
{
    if (!text || length == 0) {
        return TOKEN_IDENTIFIER;
    }
    
    /* Binary search through keyword table */
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (g_keywords[i].length == length &&
            memcmp(text, g_keywords[i].text, length) == 0) {
            return g_keywords[i].type;
        }
    }
    
    return TOKEN_IDENTIFIER;
}

static bool lexer_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool lexer_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool lexer_is_alnum(char c)
{
    return lexer_is_alpha(c) || lexer_is_digit(c);
}

static bool lexer_match_char(lexer_t *lexer, char expected)
{
    if (lexer->at_eof || lexer->current_char != expected) {
        return false;
    }
    
    lexer_advance(lexer);
    return true;
}

/* Golf-specific lexer extensions */

bool lexer_is_golf_operator(const token_t *token)
{
    if (!token) {
        return false;
    }
    
    switch (token->type) {
        case TOKEN_QUESTION:        /* ? */
        case TOKEN_COLON:          /* : */
        case TOKEN_LOGICAL_AND:    /* && */
        case TOKEN_LOGICAL_OR:     /* || */
        case TOKEN_LOGICAL_NOT:    /* ! */
        case TOKEN_CHAIN:          /* >> */
        case TOKEN_ARROW:          /* -> */
        case TOKEN_IMPLIES:        /* => */
        case TOKEN_PIPE:           /* | */
        case TOKEN_AMPERSAND:      /* & */
        case TOKEN_TILDE:          /* ~ */
            return true;
        default:
            return false;
    }
}

bool lexer_is_golf_keyword(const token_t *token)
{
    if (!token) {
        return false;
    }
    
    switch (token->type) {
        case TOKEN_WIN:
        case TOKEN_LOSE:
        case TOKEN_DRAW:
        case TOKEN_SKIP:
        case TOKEN_FAIL:
        case TOKEN_PASS:
        case TOKEN_WILDCARD:       /* _ */
        case TOKEN_ANY:
        case TOKEN_ALL:
            return true;
        default:
            return false;
    }
}

token_t lexer_scan_golf_shorthand(lexer_t *lexer)
{
    if (!lexer || lexer->at_eof) {
        return lexer_make_error_token(lexer, "Unexpected end of input");
    }
    
    lexer_mark_token_start(lexer);
    
    /* Look for common golf patterns */
    char c = lexer->current_char;
    
    /* Single character consequences - golf style */
    switch (c) {
        case 'W':  /* Win */
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOKEN_WIN);
        case 'L':  /* Lose */
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOKEN_LOSE);
        case 'D':  /* Draw */
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOKEN_DRAW);
        case 'S':  /* Skip */
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOKEN_SKIP);
        case 'F':  /* Fail */
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOKEN_FAIL);
        case 'P':  /* Pass */
            lexer_advance(lexer);
            return lexer_make_token(lexer, TOKEN_PASS);
    }
    
    /* Multi-character golf patterns */
    if (c == '>' && lexer_peek(lexer, 1) == '>') {
        /* Chain operator >> */
        lexer_advance(lexer);
        lexer_advance(lexer);
        return lexer_make_token(lexer, TOKEN_CHAIN);
    }
    
    if (c == '=' && lexer_peek(lexer, 1) == '>') {
        /* Implies operator => */
        lexer_advance(lexer);
        lexer_advance(lexer);
        return lexer_make_token(lexer, TOKEN_IMPLIES);
    }
    
    /* Fall back to regular scanning */
    return lexer_next_token(lexer);
}

/* Context-sensitive tokenization for decision trees */

typedef enum {
    LEXER_CONTEXT_DEFAULT,
    LEXER_CONTEXT_CONDITION,
    LEXER_CONTEXT_CONSEQUENCE,
    LEXER_CONTEXT_RULE_BODY
} lexer_context_t;

static lexer_context_t g_current_context = LEXER_CONTEXT_DEFAULT;

void lexer_set_context(lexer_context_t context)
{
    g_current_context = context;
}

lexer_context_t lexer_get_context(void)
{
    return g_current_context;
}

token_t lexer_next_context_token(lexer_t *lexer)
{
    if (!lexer) {
        return (token_t){.type = TOKEN_ERROR, .line = 0, .column = 0};
    }
    
    token_t token = lexer_next_token(lexer);
    
    /* Context-sensitive token interpretation */
    switch (g_current_context) {
        case LEXER_CONTEXT_CONDITION:
            /* In condition context, interpret some tokens differently */
            if (token.type == TOKEN_IDENTIFIER) {
                /* Check for implicit comparisons in golf mode */
                if (lexer->options.golf_mode) {
                    char next = lexer_peek(lexer, 0);
                    if (lexer_is_digit(next)) {
                        /* Pattern like "x5" -> "x > 5" */
                        /* This would need more complex parsing */
                    }
                }
            }
            break;
            
        case LEXER_CONTEXT_CONSEQUENCE:
            /* In consequence context, bare identifiers might be actions */
            if (token.type == TOKEN_IDENTIFIER && lexer->options.golf_mode) {
                /* Convert common patterns */
                if (token.value) {
                    if (strcmp(token.value, "w") == 0) {
                        token.type = TOKEN_WIN;
                    } else if (strcmp(token.value, "l") == 0) {
                        token.type = TOKEN_LOSE;
                    } else if (strcmp(token.value, "d") == 0) {
                        token.type = TOKEN_DRAW;
                    }
                }
            }
            break;
            
        case LEXER_CONTEXT_RULE_BODY:
            /* Rule body context - no special handling yet */
            break;
            
        case LEXER_CONTEXT_DEFAULT:
        default:
            /* Default context - no changes */
            break;
    }
    
    return token;
}

/* Lexer configuration and options */

void lexer_set_options(lexer_t *lexer, const lexer_options_t *options)
{
    if (!lexer || !options) {
        return;
    }
    
    lexer->options = *options;
    LOG_DEBUG("Updated lexer options: golf_mode=%s, case_sensitive=%s", 
              options->golf_mode ? "true" : "false",
              options->case_sensitive ? "true" : "false");
}

void lexer_get_options(const lexer_t *lexer, lexer_options_t *options)
{
    if (!lexer || !options) {
        return;
    }
    
    *options = lexer->options;
}

/* Utility functions for token stream processing */

bool lexer_consume_token(lexer_t *lexer, token_type_t expected, token_t *consumed)
{
    if (!lexer) {
        return false;
    }
    
    token_t token = lexer_next_token(lexer);
    
    if (consumed) {
        *consumed = token;
    }
    
    if (token.type == expected) {
        return true;
    }
    
    /* Token mismatch */
    if (!consumed) {
        token_free(&token);
    }
    
    return false;
}

bool lexer_expect_token(lexer_t *lexer, token_type_t expected, const char *context)
{
    if (!lexer) {
        return false;
    }
    
    token_t token = lexer_peek_token(lexer, 0);
    
    if (token.type != expected) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "%s: expected %s but found %s",
                context ? context : "Parse error",
                lexer_token_name(expected),
                lexer_token_name(token.type));
        
        LOG_ERROR("%s at line %zu, column %zu", error_msg, token.line, token.column);
        error_set(ERROR_SYNTAX, error_msg);
        
        lexer->errors_encountered++;
        return false;
    }
    
    return true;
}

void lexer_synchronize(lexer_t *lexer)
{
    if (!lexer) {
        return;
    }
    
    lexer->error_recovery_mode = true;
    
    /* Skip tokens until we find a synchronization point */
    while (!lexer_at_end(lexer)) {
        token_t token = lexer_peek_token(lexer, 0);
        
        /* Synchronization points for recovery */
        switch (token.type) {
            case TOKEN_SEMICOLON:
            case TOKEN_END:
            case TOKEN_RULE:
            case TOKEN_IF:
            case TOKEN_RBRACE:
                lexer->error_recovery_mode = false;
                return;
            default:
                break;
        }
        
        lexer_next_token(lexer);  /* Skip this token */
    }
    
    lexer->error_recovery_mode = false;
}
