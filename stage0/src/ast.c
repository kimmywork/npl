/*
 * ast.c — AST node allocation, symbol table, and pretty printer.
 *
 * Memory model: all nodes go into a linked list of arenas.  ast_free_all()
 * releases everything at once.  Symbols are interned in a simple hash table
 * and never freed (they live for the process lifetime — fine for a compiler).
 */

#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ---------- arena allocator ---------- */

#define ARENA_BLOCK_SIZE 4096  /* nodes per block */

typedef struct ArenaBlock {
    AstNode nodes[ARENA_BLOCK_SIZE];
    size_t  used;
    struct ArenaBlock *next;
} ArenaBlock;

static ArenaBlock *g_arena_head = NULL;

static AstNode *ast_alloc(void)
{
    if (!g_arena_head || g_arena_head->used >= ARENA_BLOCK_SIZE) {
        ArenaBlock *b = (ArenaBlock *)calloc(1, sizeof(ArenaBlock));
        if (!b) { perror("ast_alloc"); exit(1); }
        b->next = g_arena_head;
        g_arena_head = b;
    }
    return &g_arena_head->nodes[g_arena_head->used++];
}

void ast_free_all(void)
{
    ArenaBlock *b = g_arena_head;
    while (b) {
        ArenaBlock *next = b->next;
        /* Free owned strings in string nodes */
        for (size_t i = 0; i < b->used; i++) {
            if (b->nodes[i].tag == AST_STRING && b->nodes[i].as.string) {
                free(b->nodes[i].as.string);
            }
        }
        free(b);
        b = next;
    }
    g_arena_head = NULL;
}

/* ---------- symbol table (simple open-addressing hash) ---------- */

#define SYM_TABLE_SIZE 4096

static const char *g_sym_table[SYM_TABLE_SIZE];

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h ^= (uint8_t)*s;
        h *= 16777619u;
    }
    return h;
}

const char *sym_intern(const char *name)
{
    uint32_t h = fnv1a(name) & (SYM_TABLE_SIZE - 1);
    while (g_sym_table[h]) {
        if (strcmp(g_sym_table[h], name) == 0)
            return g_sym_table[h];
        h = (h + 1) & (SYM_TABLE_SIZE - 1);
    }
    /* Not found — duplicate and store */
    char *copy = strdup(name);
    if (!copy) { perror("sym_intern"); exit(1); }
    g_sym_table[h] = copy;
    return copy;
}

/* ---------- constructors ---------- */

AstNode *ast_nil(void)
{
    AstNode *n = ast_alloc();
    n->tag = AST_NIL;
    return n;
}

AstNode *ast_bool(int val)
{
    AstNode *n = ast_alloc();
    n->tag = AST_BOOL;
    n->as.bool_val = val;
    return n;
}

AstNode *ast_fixnum(int64_t val)
{
    AstNode *n = ast_alloc();
    n->tag = AST_FIXNUM;
    n->as.fixnum = val;
    return n;
}

AstNode *ast_flonum(double val)
{
    AstNode *n = ast_alloc();
    n->tag = AST_FLONUM;
    n->as.flonum = val;
    return n;
}

AstNode *ast_symbol(const char *name)
{
    AstNode *n = ast_alloc();
    n->tag = AST_SYMBOL;
    n->as.symbol = sym_intern(name);
    return n;
}

AstNode *ast_string(char *s)
{
    AstNode *n = ast_alloc();
    n->tag = AST_STRING;
    n->as.string = s;  /* takes ownership */
    return n;
}

AstNode *ast_pair(AstNode *car, AstNode *cdr)
{
    AstNode *n = ast_alloc();
    n->tag = AST_PAIR;
    n->as.pair.car = car;
    n->as.pair.cdr = cdr;
    return n;
}

AstNode *ast_if(AstNode *test, AstNode *conseq, AstNode *alt)
{
    AstNode *n = ast_alloc();
    n->tag = AST_IF;
    n->as.pair.car = test;
    n->as.pair.cdr = ast_pair(conseq, ast_pair(alt, ast_nil()));
    return n;
}

AstNode *ast_define(AstNode *name_or_sig, AstNode *body)
{
    AstNode *n = ast_alloc();
    n->tag = AST_DEFINE;
    n->as.pair.car = name_or_sig;
    n->as.pair.cdr = body;
    return n;
}

AstNode *ast_lambda(AstNode *params, AstNode *body)
{
    AstNode *n = ast_alloc();
    n->tag = AST_LAMBDA;
    n->as.pair.car = params;
    n->as.pair.cdr = body;
    return n;
}

AstNode *ast_let(AstNode *bindings, AstNode *body)
{
    AstNode *n = ast_alloc();
    n->tag = AST_LET;
    n->as.pair.car = bindings;
    n->as.pair.cdr = body;
    return n;
}

AstNode *ast_begin(AstNode *exprs)
{
    AstNode *n = ast_alloc();
    n->tag = AST_BEGIN;
    n->as.pair.car = exprs;
    n->as.pair.cdr = NULL;
    return n;
}

AstNode *ast_quote(AstNode *datum)
{
    AstNode *n = ast_alloc();
    n->tag = AST_QUOTE;
    n->as.pair.car = datum;
    n->as.pair.cdr = NULL;
    return n;
}

AstNode *ast_set(AstNode *name, AstNode *value)
{
    AstNode *n = ast_alloc();
    n->tag = AST_SET;
    n->as.pair.car = name;
    n->as.pair.cdr = value;
    return n;
}

/* ---------- pretty printer ---------- */

static void print_list_contents(const AstNode *node)
{
    if (AST_IS_NIL(node)) return;

    if (node->tag == AST_PAIR) {
        ast_print(AST_CAR(node));
        if (AST_CDR(node) && !AST_IS_NIL(AST_CDR(node))) {
            if (AST_CDR(node)->tag == AST_PAIR) {
                printf(" ");
                print_list_contents(AST_CDR(node));
            } else {
                printf(" . ");
                ast_print(AST_CDR(node));
            }
        }
    } else {
        /* improper list ending */
        printf(" . ");
        ast_print(node);
    }
}

void ast_print(const AstNode *node)
{
    if (!node) { printf("<null>"); return; }

    switch (node->tag) {
    case AST_NIL:
        printf("()");
        break;
    case AST_BOOL:
        printf("%s", node->as.bool_val ? "#t" : "#f");
        break;
    case AST_FIXNUM:
        printf("%lld", (long long)node->as.fixnum);
        break;
    case AST_FLONUM:
        printf("%g", node->as.flonum);
        break;
    case AST_SYMBOL:
        printf("%s", node->as.symbol);
        break;
    case AST_STRING:
        printf("\"%s\"", node->as.string);
        break;
    case AST_PAIR:
        printf("(");
        print_list_contents(node);
        printf(")");
        break;
    case AST_IF:
        printf("(if ");
        ast_print(AST_CAR(node));          /* test */
        printf(" ");
        ast_print(AST_CAR(AST_CDR(node))); /* consequent */
        if (AST_CDR(AST_CDR(node)) && !AST_IS_NIL(AST_CDR(AST_CDR(node)))) {
            printf(" ");
            ast_print(AST_CAR(AST_CDR(AST_CDR(node)))); /* alternate */
        }
        printf(")");
        break;
    case AST_DEFINE:
        printf("(define ");
        ast_print(AST_CAR(node));  /* name or signature */
        if (node->as.pair.car->tag == AST_PAIR) {
            /* function define: (define (f args...) body...) — flat body */
            const AstNode *cur = AST_CDR(node);
            while (cur && !AST_IS_NIL(cur)) {
                printf(" ");
                ast_print(cur->as.pair.car);
                cur = cur->as.pair.cdr;
            }
        } else {
            /* variable define: (define x value) */
            printf(" ");
            ast_print(AST_CDR(node));
        }
        printf(")");
        break;
    case AST_LAMBDA:
        printf("(lambda ");
        ast_print(AST_CAR(node));  /* params */
        {
            /* body: sequence of expressions, printed flat */
            const AstNode *cur = AST_CDR(node);
            while (cur && !AST_IS_NIL(cur)) {
                printf(" ");
                ast_print(cur->as.pair.car);
                cur = cur->as.pair.cdr;
            }
        }
        printf(")");
        break;
    case AST_LET:
        printf("(let ");
        ast_print(AST_CAR(node));  /* bindings */
        {
            /* body: sequence of expressions, printed flat */
            const AstNode *cur = AST_CDR(node);
            while (cur && !AST_IS_NIL(cur)) {
                printf(" ");
                ast_print(cur->as.pair.car);
                cur = cur->as.pair.cdr;
            }
        }
        printf(")");
        break;
    case AST_BEGIN:
        printf("(begin");
        {
            const AstNode *cur = AST_CAR(node);
            while (cur && !AST_IS_NIL(cur)) {
                printf(" ");
                ast_print(AST_CAR(cur));
                cur = AST_CDR(cur);
            }
        }
        printf(")");
        break;
    case AST_QUOTE:
        printf("(quote ");
        ast_print(AST_CAR(node));
        printf(")");
        break;
    case AST_SET:
        printf("(set! ");
        ast_print(AST_CAR(node));
        printf(" ");
        ast_print(AST_CDR(node));
        printf(")");
        break;
    }
}
