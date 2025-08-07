#ifndef REASONS_PARSER_H
#define REASONS_PARSER_H

#include "reasons/ast.h"
#include "reasons/lexer.h"
#include "reasons/types.h"

/* Maximum recursion depth to prevent stack overflow */
#define MAX_RECURSION_DEPTH 256

/* Opaque parser structure */
typedef struct parser_state parser_t;

/* Parser creation and destruction */
parser_t *parser_create(lexer_t *lexer);
void parser_destroy(parser_t *parser);

/* Main parsing function */
ast_node_t *parser_parse(parser_t *parser);

/* Context control */
void parser_set_condition_context(parser_t *parser, bool enabled);
void parser_set_consequence_context(parser_t *parser, bool enabled);

/* Error checking */
bool parser_had_error(const parser_t *parser);

#endif /* REASONS_PARSER_H */
