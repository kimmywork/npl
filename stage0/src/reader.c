#include "reader.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TOKEN_LEFT,
    TOKEN_RIGHT,
    TOKEN_QUOTE,
    TOKEN_ATOM,
    TOKEN_STRING,
    TOKEN_EOF,
    TOKEN_ERROR
} TokenKind;

typedef struct {
    TokenKind kind;
    char *text;
    int line;
    int column;
} Token;

typedef struct {
    const char *source;
    size_t offset;
    int line;
    int column;
} Lexer;

typedef struct {
    Lexer lexer;
    Token current;
    char *error;
    size_t error_size;
    int failed;
} Parser;

static char *copy_text(const char *start, size_t length)
{
    char *text = malloc(length + 1);
    if (!text) {
        perror("malloc");
        exit(1);
    }
    memcpy(text, start, length);
    text[length] = '\0';
    return text;
}

static char lexer_peek(const Lexer *lexer)
{
    return lexer->source[lexer->offset];
}

static char lexer_advance(Lexer *lexer)
{
    char value = lexer->source[lexer->offset++];
    if (value == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return value;
}

static void skip_space(Lexer *lexer)
{
    for (;;) {
        while (isspace((unsigned char)lexer_peek(lexer))) {
            lexer_advance(lexer);
        }
        if (lexer_peek(lexer) != ';') {
            return;
        }
        while (lexer_peek(lexer) && lexer_peek(lexer) != '\n') {
            lexer_advance(lexer);
        }
    }
}

static Token make_token(TokenKind kind, char *text, int line, int column)
{
    Token token = { kind, text, line, column };
    return token;
}

static Token read_string(Lexer *lexer, int line, int column)
{
    lexer_advance(lexer);
    size_t capacity = 32;
    size_t length = 0;
    char *text = malloc(capacity);
    if (!text) {
        perror("malloc");
        exit(1);
    }

    while (lexer_peek(lexer) && lexer_peek(lexer) != '"') {
        char value = lexer_advance(lexer);
        if (value == '\\') {
            if (!lexer_peek(lexer)) {
                free(text);
                return make_token(TOKEN_ERROR,
                                  copy_text("unterminated string", 19),
                                  line, column);
            }
            value = lexer_advance(lexer);
            if (value == 'n') value = '\n';
            else if (value == 't') value = '\t';
        }
        if (length + 1 == capacity) {
            capacity *= 2;
            text = realloc(text, capacity);
            if (!text) {
                perror("realloc");
                exit(1);
            }
        }
        text[length++] = value;
    }

    if (!lexer_peek(lexer)) {
        free(text);
        return make_token(TOKEN_ERROR, copy_text("unterminated string", 19),
                          line, column);
    }
    lexer_advance(lexer);
    text[length] = '\0';
    return make_token(TOKEN_STRING, text, line, column);
}

static int atom_delimiter(char value)
{
    return value == '\0' || isspace((unsigned char)value) || value == '(' ||
           value == ')' || value == '\'' || value == '"' || value == ';';
}

static Token lexer_next(Lexer *lexer)
{
    skip_space(lexer);
    int line = lexer->line;
    int column = lexer->column;
    char value = lexer_peek(lexer);

    if (!value) return make_token(TOKEN_EOF, NULL, line, column);
    if (value == '(') {
        lexer_advance(lexer);
        return make_token(TOKEN_LEFT, NULL, line, column);
    }
    if (value == ')') {
        lexer_advance(lexer);
        return make_token(TOKEN_RIGHT, NULL, line, column);
    }
    if (value == '\'') {
        lexer_advance(lexer);
        return make_token(TOKEN_QUOTE, NULL, line, column);
    }
    if (value == '"') return read_string(lexer, line, column);

    size_t start = lexer->offset;
    while (!atom_delimiter(lexer_peek(lexer))) {
        lexer_advance(lexer);
    }
    return make_token(TOKEN_ATOM,
                      copy_text(lexer->source + start, lexer->offset - start),
                      line, column);
}

static void parser_error(Parser *parser, int line, int column,
                         const char *message)
{
    if (parser->failed) return;
    parser->failed = 1;
    if (parser->error && parser->error_size) {
        snprintf(parser->error, parser->error_size,
                 "line %d col %d: %s", line, column, message);
    }
}

static void parser_advance(Parser *parser)
{
    free(parser->current.text);
    parser->current = lexer_next(&parser->lexer);
    if (parser->current.kind == TOKEN_ERROR) {
        parser_error(parser, parser->current.line, parser->current.column,
                     parser->current.text);
    }
}

static AstNode *parse_expression(Parser *parser);

static AstNode *parse_list(Parser *parser, int line, int column)
{
    AstNode *head = ast_nil();
    AstNode *tail = NULL;

    while (!parser->failed && parser->current.kind != TOKEN_RIGHT) {
        if (parser->current.kind == TOKEN_EOF) {
            parser_error(parser, line, column,
                         "unmatched '('; expected ')' before end of input");
            return NULL;
        }
        AstNode *item = parse_expression(parser);
        if (!item) return NULL;
        AstNode *cell = ast_pair(item, ast_nil());
        if (tail) AST_CDR(tail) = cell;
        else head = cell;
        tail = cell;
    }

    if (!parser->failed) parser_advance(parser);
    return head;
}

static AstNode *parse_atom(Parser *parser)
{
    const char *text = parser->current.text;
    AstNode *node;

    if (strcmp(text, "#t") == 0 || strcmp(text, "#f") == 0) {
        node = ast_bool(text[1] == 't');
    } else {
        char *end = NULL;
        errno = 0;
        long long value = strtoll(text, &end, 10);
        if (text[0] && *end == '\0' && errno != ERANGE) {
            node = ast_fixnum((int64_t)value);
        } else {
            node = ast_symbol(text);
        }
    }
    parser_advance(parser);
    return node;
}

static AstNode *parse_expression(Parser *parser)
{
    Token token = parser->current;
    switch (token.kind) {
    case TOKEN_LEFT:
        parser_advance(parser);
        return parse_list(parser, token.line, token.column);
    case TOKEN_RIGHT:
        parser_error(parser, token.line, token.column,
                     "unexpected closing delimiter");
        return NULL;
    case TOKEN_QUOTE: {
        parser_advance(parser);
        AstNode *datum = parse_expression(parser);
        if (!datum) return NULL;
        return ast_pair(ast_symbol("quote"),
                        ast_pair(datum, ast_nil()));
    }
    case TOKEN_ATOM:
        return parse_atom(parser);
    case TOKEN_STRING: {
        char *text = parser->current.text;
        parser->current.text = NULL;
        AstNode *node = ast_string(text);
        parser_advance(parser);
        return node;
    }
    case TOKEN_EOF:
        parser_error(parser, token.line, token.column,
                     "unexpected end of input");
        return NULL;
    case TOKEN_ERROR:
        return NULL;
    }
    return NULL;
}

AstNode *reader_read_all(const char *source, char *error, size_t error_size)
{
    Parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.lexer.source = source;
    parser.lexer.line = 1;
    parser.lexer.column = 1;
    parser.error = error;
    parser.error_size = error_size;
    parser.current = lexer_next(&parser.lexer);

    AstNode *head = ast_nil();
    AstNode *tail = NULL;
    while (!parser.failed && parser.current.kind != TOKEN_EOF) {
        AstNode *expression = parse_expression(&parser);
        if (!expression) break;
        AstNode *cell = ast_pair(expression, ast_nil());
        if (tail) AST_CDR(tail) = cell;
        else head = cell;
        tail = cell;
    }

    free(parser.current.text);
    return parser.failed ? NULL : head;
}

char *reader_read_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *source = malloc((size_t)size + 1);
    if (!source) {
        fclose(file);
        return NULL;
    }
    size_t read_size = fread(source, 1, (size_t)size, file);
    fclose(file);
    source[read_size] = '\0';
    return source;
}
