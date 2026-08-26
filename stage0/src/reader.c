/*
 * reader.c — Lexer + recursive-descent parser for NPL S-expressions.
 *
 * Reads from a string buffer and produces AST nodes.  Handles:
 *   - Parentheses: ( ) [ ]
 *   - Quote: '
 *   - Symbols, integers, floats, strings
 *   - Booleans: #t, #f
 *   - Comments: ; to end of line
 *   - Special forms: define, lambda, let, if, begin, quote, set!
 *
 * The reader is intentionally simple: one token of lookahead, no
 * backtracking.  Special forms are detected after parsing a list.
 */

#include "reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

/* ---------- token types ---------- */

typedef enum {
    TOK_LPAREN,   /* ( */
    TOK_RPAREN,   /* ) */
    TOK_LBRACKET, /* [ */
    TOK_RBRACKET, /* ] */
    TOK_QUOTE,    /* ' */
    TOK_SYMBOL,
    TOK_NUMBER,   /* integer or float */
    TOK_STRING,
    TOK_BOOL,
    TOK_EOF,
    TOK_ERROR,
} TokType;

typedef struct {
    TokType  type;
    char    *text;    /* heap-allocated token text */
    int      line;
    int      col;
} Token;

/* ---------- lexer state ---------- */

typedef struct {
    const char *input;
    size_t      pos;
    int         line;
    int         col;
    Token       current;  /* one-token lookahead */
} Lexer;

static void lexer_init(Lexer *lex, const char *input)
{
    lex->input  = input;
    lex->pos    = 0;
    lex->line   = 1;
    lex->col    = 1;
    memset(&lex->current, 0, sizeof(lex->current));
}

static char peek(Lexer *lex)
{
    return lex->input[lex->pos];
}

static char advance(Lexer *lex)
{
    char c = lex->input[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else { lex->col++; }
    return c;
}

static void skip_whitespace_and_comments(Lexer *lex)
{
    for (;;) {
        char c = peek(lex);
        if (c == '\0') return;
        if (isspace((unsigned char)c)) { advance(lex); continue; }
        if (c == ';') {
            /* line comment — skip to end of line */
            while (peek(lex) != '\0' && peek(lex) != '\n')
                advance(lex);
            continue;
        }
        return;
    }
}

/* Read a string literal (after the opening '"').  Returns a malloc'd copy. */
static char *read_string_lit(Lexer *lex)
{
    /* skip opening quote */
    advance(lex);
    size_t cap = 64, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) { perror("read_string_lit"); exit(1); }

    for (;;) {
        char c = peek(lex);
        if (c == '\0') {
            free(buf);
            return NULL; /* unterminated string */
        }
        if (c == '"') {
            advance(lex);
            break;
        }
        if (c == '\\') {
            advance(lex);
            char esc = advance(lex);
            switch (esc) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case '\\': c = '\\'; break;
            case '"': c = '"'; break;
            default: c = esc; break;
            }
        } else {
            advance(lex);
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)realloc(buf, cap);
            if (!buf) { perror("read_string_lit"); exit(1); }
        }
        buf[len++] = c;
    }
    buf[len] = '\0';
    return buf;
}

/* Read a token.  Does NOT skip whitespace — caller must do that. */
static Token next_token(Lexer *lex)
{
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.line = lex->line;
    tok.col  = lex->col;

    skip_whitespace_and_comments(lex);

    char c = peek(lex);
    if (c == '\0') {
        tok.type = TOK_EOF;
        tok.text = strdup("");
        return tok;
    }

    /* single-char tokens */
    if (c == '(') { advance(lex); tok.type = TOK_LPAREN; tok.text = strdup("("); return tok; }
    if (c == ')') { advance(lex); tok.type = TOK_RPAREN; tok.text = strdup(")"); return tok; }
    if (c == '[') { advance(lex); tok.type = TOK_LBRACKET; tok.text = strdup("["); return tok; }
    if (c == ']') { advance(lex); tok.type = TOK_RBRACKET; tok.text = strdup("]"); return tok; }
    if (c == '\'') { advance(lex); tok.type = TOK_QUOTE; tok.text = strdup("'"); return tok; }

    /* string literal */
    if (c == '"') {
        tok.type = TOK_STRING;
        tok.text = read_string_lit(lex);
        if (!tok.text) { tok.type = TOK_ERROR; tok.text = strdup("unterminated string"); }
        return tok;
    }

    /* boolean or character */
    if (c == '#') {
        advance(lex);
        char next = peek(lex);
        if (next == 't' || next == 'T') {
            advance(lex);
            tok.type = TOK_BOOL;
            tok.text = strdup("#t");
        } else if (next == 'f' || next == 'F') {
            advance(lex);
            tok.type = TOK_BOOL;
            tok.text = strdup("#f");
        } else {
            tok.type = TOK_ERROR;
            tok.text = strdup("bad #-literal");
        }
        return tok;
    }

    /* number or symbol */
    if (isdigit((unsigned char)c) || c == '-' || c == '+') {
        /* Peek ahead to see if it's a number or a symbol like +, -, +foo */
        int is_num = 0;
        if (isdigit((unsigned char)c)) {
            is_num = 1;
        } else if ((c == '-' || c == '+') && lex->input[lex->pos + 1] != '\0' &&
                   isdigit((unsigned char)lex->input[lex->pos + 1])) {
            is_num = 1;
        }

        if (is_num) {
            size_t start = lex->pos;
            if (c == '-' || c == '+') advance(lex);
            while (isdigit((unsigned char)peek(lex))) advance(lex);
            if (peek(lex) == '.') advance(lex);
            while (isdigit((unsigned char)peek(lex))) advance(lex);

            size_t len = lex->pos - start;
            tok.text = (char *)malloc(len + 1);
            memcpy(tok.text, lex->input + start, len);
            tok.text[len] = '\0';
            tok.type = TOK_NUMBER;
            return tok;
        }
    }

    /* symbol (identifiers, operators like +, -, *, etc.) */
    {
        size_t start = lex->pos;
        /* Symbols can contain: letters, digits, -, _, *, /, ?, !, =, <, >, $, %, &, @, . */
        while (peek(lex) != '\0' && !isspace((unsigned char)peek(lex)) &&
               peek(lex) != '(' && peek(lex) != ')' &&
               peek(lex) != '[' && peek(lex) != ']' &&
               peek(lex) != '\'' && peek(lex) != '"' &&
               peek(lex) != ';')
        {
            advance(lex);
        }
        size_t len = lex->pos - start;
        if (len == 0) {
            /* shouldn't happen, but just in case */
            advance(lex);
            tok.type = TOK_ERROR;
            tok.text = strdup("unexpected char");
            return tok;
        }
        tok.text = (char *)malloc(len + 1);
        memcpy(tok.text, lex->input + start, len);
        tok.text[len] = '\0';
        tok.type = TOK_SYMBOL;
        return tok;
    }
}

/* ---------- parser ---------- */

typedef struct {
    Lexer  lexer;
    int    had_error;
    char   error_msg[256];
} Parser;

static void parser_init(Parser *p, const char *input)
{
    lexer_init(&p->lexer, input);
    p->had_error = 0;
    p->error_msg[0] = '\0';
    /* prime the lookahead */
    p->lexer.current = next_token(&p->lexer);
}

static Token cur(Parser *p)
{
    return p->lexer.current;
}

static Token consume(Parser *p)
{
    Token t = p->lexer.current;
    p->lexer.current = next_token(&p->lexer);
    return t;
}

static int check(Parser *p, TokType type)
{
    return cur(p).type == type;
}

static void parse_error(Parser *p, const char *msg)
{
    if (!p->had_error) {
        p->had_error = 1;
        snprintf(p->error_msg, sizeof(p->error_msg),
                 "parse error at line %d col %d: %s (got '%s')",
                 cur(p).line, cur(p).col, msg, cur(p).text);
    }
}

/* Check if a symbol is a special form keyword */
static AstTag special_form_tag(const char *sym)
{
    if (strcmp(sym, "if") == 0)     return AST_IF;
    if (strcmp(sym, "define") == 0) return AST_DEFINE;
    if (strcmp(sym, "lambda") == 0) return AST_LAMBDA;
    if (strcmp(sym, "let") == 0)    return AST_LET;
    if (strcmp(sym, "begin") == 0)  return AST_BEGIN;
    if (strcmp(sym, "quote") == 0)  return AST_QUOTE;
    if (strcmp(sym, "set!") == 0)   return AST_SET;
    return AST_PAIR; /* not a special form */
}

/* Forward decl */
static AstNode *parse_expr(Parser *p);

/* Parse a list of expressions until closing paren */
static AstNode *parse_list(Parser *p)
{
    if (check(p, TOK_RPAREN) || check(p, TOK_RBRACKET)) {
        return ast_nil();
    }
    if (check(p, TOK_EOF)) {
        parse_error(p, "unexpected end of input in list");
        return ast_nil();
    }

    AstNode *car = parse_expr(p);
    if (p->had_error) return ast_nil();

    AstNode *cdr = parse_list(p);
    return ast_pair(car, cdr);
}

/* Parse bindings list for let: ((name val) ...) */
static AstNode *parse_let_bindings(Parser *p)
{
    if (check(p, TOK_RPAREN)) {
        return ast_nil();
    }
    if (check(p, TOK_EOF)) {
        parse_error(p, "unexpected end of input in let bindings");
        return ast_nil();
    }

    /* Each binding is (name val) */
    if (!check(p, TOK_LPAREN)) {
        parse_error(p, "expected '(' to start let binding");
        return ast_nil();
    }
    consume(p); /* consume '(' */

    AstNode *name = parse_expr(p);
    AstNode *val  = parse_expr(p);

    if (!check(p, TOK_RPAREN)) {
        parse_error(p, "expected ')' to close let binding");
        return ast_nil();
    }
    consume(p); /* consume ')' */

    AstNode *binding = ast_pair(name, ast_pair(val, ast_nil()));
    AstNode *rest = parse_let_bindings(p);
    return ast_pair(binding, rest);
}

/* Parse a (possibly special) list form */
static AstNode *parse_list_form(Parser *p)
{
    Token open = consume(p); /* consume '(' or '[' */
    int is_bracket = (open.type == TOK_LBRACKET);

    if (check(p, TOK_RPAREN) || check(p, TOK_RBRACKET)) {
        consume(p);
        return ast_nil();
    }

    /* Check if first element is a special form keyword */
    if (check(p, TOK_SYMBOL)) {
        AstTag tag = special_form_tag(cur(p).text);
        if (tag != AST_PAIR) {
            consume(p); /* consume keyword */

            if (tag == AST_IF) {
                AstNode *test   = parse_expr(p);
                AstNode *conseq = parse_expr(p);
                AstNode *alt    = ast_nil();
                if (!check(p, TOK_RPAREN) && !check(p, TOK_RBRACKET)) {
                    alt = parse_expr(p);
                }
                /* consume closing paren */
                if (is_bracket) {
                    if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
                    else consume(p);
                } else {
                    if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
                    else consume(p);
                }
                return ast_if(test, conseq, alt);
            }

            if (tag == AST_DEFINE) {
                /* Could be (define name value) or (define (name args...) body) */
                if (check(p, TOK_LPAREN)) {
                    /* Function shorthand: (define (name args...) body) */
                    consume(p); /* ( */
                    AstNode *name = parse_expr(p);  /* function name */
                    /* Read parameter list */
                    AstNode *params = parse_list(p);
                    if (!check(p, TOK_RPAREN)) { parse_error(p, "expected ')'"); return ast_nil(); }
                    consume(p); /* ) */
                    /* Body */
                    AstNode *body = parse_expr(p);
                    /* (define (f x) body) → define name=lambda(params, body) */
                    AstNode *sig = ast_pair(name, params);
                    /* consume closing paren */
                    if (is_bracket) {
                        if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
                        else consume(p);
                    } else {
                        if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
                        else consume(p);
                    }
                    return ast_define(sig, body);
                } else {
                    /* Simple: (define name value) */
                    AstNode *name = parse_expr(p);
                    AstNode *val  = parse_expr(p);
                    /* consume closing paren */
                    if (is_bracket) {
                        if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
                        else consume(p);
                    } else {
                        if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
                        else consume(p);
                    }
                    return ast_define(name, val);
                }
            }

            if (tag == AST_LAMBDA) {
                /* (lambda (params...) body) */
                AstNode *params;
                if (check(p, TOK_LPAREN) || check(p, TOK_LBRACKET)) {
                    consume(p); /* open */
                    params = parse_list(p);
                    if (!check(p, TOK_RPAREN) && !check(p, TOK_RBRACKET)) {
                        parse_error(p, "expected ')' or ']' to close lambda params");
                        return ast_nil();
                    }
                    consume(p); /* close */
                } else {
                    parse_error(p, "expected parameter list after lambda");
                    return ast_nil();
                }
                AstNode *body = parse_expr(p);
                /* consume closing paren */
                if (is_bracket) {
                    if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
                    else consume(p);
                } else {
                    if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
                    else consume(p);
                }
                return ast_lambda(params, body);
            }

            if (tag == AST_LET) {
                /* (let ((name val) ...) body) */
                if (!check(p, TOK_LPAREN)) {
                    parse_error(p, "expected '(' to start let bindings");
                    return ast_nil();
                }
                consume(p); /* consume '(' */
                AstNode *bindings = parse_let_bindings(p);
                if (!check(p, TOK_RPAREN)) {
                    parse_error(p, "expected ')' to close let bindings");
                    return ast_nil();
                }
                consume(p); /* consume ')' */
                AstNode *body = parse_expr(p);
                /* consume closing paren */
                if (is_bracket) {
                    if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
                    else consume(p);
                } else {
                    if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
                    else consume(p);
                }
                return ast_let(bindings, body);
            }

            if (tag == AST_BEGIN) {
                /* (begin exprs...) */
                AstNode *exprs = parse_list(p);
                /* consume closing paren */
                if (is_bracket) {
                    if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
                    else consume(p);
                } else {
                    if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
                    else consume(p);
                }
                return ast_begin(exprs);
            }

            if (tag == AST_QUOTE) {
                /* (quote datum) */
                AstNode *datum = parse_expr(p);
                /* consume closing paren */
                if (is_bracket) {
                    if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
                    else consume(p);
                } else {
                    if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
                    else consume(p);
                }
                return ast_quote(datum);
            }

            if (tag == AST_SET) {
                /* (set! name value) */
                AstNode *name = parse_expr(p);
                AstNode *val  = parse_expr(p);
                /* consume closing paren */
                if (is_bracket) {
                    if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
                    else consume(p);
                } else {
                    if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
                    else consume(p);
                }
                return ast_set(name, val);
            }
        }
    }

    /* Regular list (application form) */
    AstNode *items = parse_list(p);
    /* consume closing paren */
    if (is_bracket) {
        if (!check(p, TOK_RBRACKET)) parse_error(p, "expected ']'");
        else consume(p);
    } else {
        if (!check(p, TOK_RPAREN)) parse_error(p, "expected ')'");
        else consume(p);
    }
    return items;
}

/* Parse one expression */
static AstNode *parse_expr(Parser *p)
{
    if (p->had_error) return ast_nil();

    Token t = cur(p);

    switch (t.type) {
    case TOK_EOF:
        parse_error(p, "unexpected end of input");
        return ast_nil();

    case TOK_RPAREN:
    case TOK_RBRACKET:
        parse_error(p, "unexpected closing bracket");
        return ast_nil();

    case TOK_LPAREN:
    case TOK_LBRACKET:
        return parse_list_form(p);

    case TOK_QUOTE:
        consume(p);
        return ast_quote(parse_expr(p));

    case TOK_BOOL:
        consume(p);
        return ast_bool(t.text[1] == 't');

    case TOK_NUMBER: {
        consume(p);
        /* Try integer first */
        char *end;
        long long ival = strtoll(t.text, &end, 10);
        if (*end == '\0') {
            free(t.text);
            return ast_fixnum((int64_t)ival);
        }
        /* Try float */
        double fval = strtod(t.text, &end);
        if (*end == '\0') {
            free(t.text);
            return ast_flonum(fval);
        }
        free(t.text);
        parse_error(p, "invalid number");
        return ast_nil();
    }

    case TOK_STRING: {
        consume(p);
        /* ast_string takes ownership of the malloc'd text */
        return ast_string(t.text);
    }

    case TOK_SYMBOL: {
        consume(p);
        AstNode *sym = ast_symbol(t.text);
        free(t.text);
        return sym;
    }

    case TOK_ERROR:
        consume(p);
        parse_error(p, t.text);
        free(t.text);
        return ast_nil();
    }

    return ast_nil(); /* unreachable */
}

/* ---------- public API ---------- */

AstNode *reader_read(const char *input, char *errbuf, size_t errbuf_size)
{
    Parser p;
    parser_init(&p, input);

    if (check(&p, TOK_EOF)) {
        return ast_nil();
    }

    AstNode *result = parse_expr(&p);

    if (p.had_error) {
        if (errbuf && errbuf_size > 0) {
            strncpy(errbuf, p.error_msg, errbuf_size - 1);
            errbuf[errbuf_size - 1] = '\0';
        }
        return NULL;
    }

    return result;
}

AstNode *reader_read_all(const char *input, char *errbuf, size_t errbuf_size)
{
    Parser p;
    parser_init(&p, input);

    /* Read all top-level expressions into a list */
    AstNode *result = ast_nil();
    AstNode **tail = &result;

    while (!check(&p, TOK_EOF) && !p.had_error) {
        AstNode *expr = parse_expr(&p);
        if (p.had_error) break;
        /* Prepend (we'll reverse later if needed — but for now, build in order) */
        *tail = ast_pair(expr, ast_nil());
        tail = &((*tail)->as.pair.cdr);
    }

    if (p.had_error) {
        if (errbuf && errbuf_size > 0) {
            strncpy(errbuf, p.error_msg, errbuf_size - 1);
            errbuf[errbuf_size - 1] = '\0';
        }
        return NULL;
    }

    return result;
}

/* Convenience: read from file */
char *reader_read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t nread = fread(buf, 1, sz, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}
