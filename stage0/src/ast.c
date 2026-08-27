#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE 2048
#define SYMBOL_TABLE_SIZE 2048

typedef struct ArenaBlock ArenaBlock;
struct ArenaBlock {
    AstNode nodes[ARENA_BLOCK_SIZE];
    size_t used;
    ArenaBlock *next;
};

static ArenaBlock *arena;
static char *symbols[SYMBOL_TABLE_SIZE];

static AstNode *allocate_node(void)
{
    if (!arena || arena->used == ARENA_BLOCK_SIZE) {
        ArenaBlock *block = calloc(1, sizeof(*block));
        if (!block) {
            perror("calloc");
            exit(1);
        }
        block->next = arena;
        arena = block;
    }
    return &arena->nodes[arena->used++];
}

static uint32_t hash_symbol(const char *text)
{
    uint32_t hash = 2166136261u;
    while (*text) {
        hash ^= (unsigned char)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static const char *intern_symbol(const char *name)
{
    size_t index = hash_symbol(name) & (SYMBOL_TABLE_SIZE - 1);
    while (symbols[index]) {
        if (strcmp(symbols[index], name) == 0) {
            return symbols[index];
        }
        index = (index + 1) & (SYMBOL_TABLE_SIZE - 1);
    }

    size_t size = strlen(name) + 1;
    symbols[index] = malloc(size);
    if (!symbols[index]) {
        perror("malloc");
        exit(1);
    }
    memcpy(symbols[index], name, size);
    return symbols[index];
}

AstNode *ast_nil(void)
{
    AstNode *node = allocate_node();
    node->tag = AST_NIL;
    return node;
}

AstNode *ast_bool(int value)
{
    AstNode *node = allocate_node();
    node->tag = AST_BOOL;
    node->as.bool_value = value != 0;
    return node;
}

AstNode *ast_fixnum(int64_t value)
{
    AstNode *node = allocate_node();
    node->tag = AST_FIXNUM;
    node->as.fixnum = value;
    return node;
}

AstNode *ast_symbol(const char *name)
{
    AstNode *node = allocate_node();
    node->tag = AST_SYMBOL;
    node->as.symbol = intern_symbol(name);
    return node;
}

AstNode *ast_string(char *value)
{
    AstNode *node = allocate_node();
    node->tag = AST_STRING;
    node->as.string = value;
    return node;
}

AstNode *ast_pair(AstNode *car, AstNode *cdr)
{
    AstNode *node = allocate_node();
    node->tag = AST_PAIR;
    node->as.pair.car = car;
    node->as.pair.cdr = cdr;
    return node;
}

int ast_is_nil(const AstNode *node)
{
    return node && node->tag == AST_NIL;
}

int ast_is_pair(const AstNode *node)
{
    return node && node->tag == AST_PAIR;
}

static void print_string(const char *text)
{
    putchar('"');
    while (*text) {
        switch (*text) {
        case '\n': fputs("\\n", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '"': fputs("\\\"", stdout); break;
        default: putchar(*text); break;
        }
        text++;
    }
    putchar('"');
}

static void print_pair(const AstNode *node)
{
    putchar('(');
    while (ast_is_pair(node)) {
        ast_print(AST_CAR(node));
        node = AST_CDR(node);
        if (ast_is_pair(node)) {
            putchar(' ');
        }
    }
    if (!ast_is_nil(node)) {
        fputs(" . ", stdout);
        ast_print(node);
    }
    putchar(')');
}

void ast_print(const AstNode *node)
{
    switch (node->tag) {
    case AST_NIL: fputs("()", stdout); break;
    case AST_BOOL: fputs(node->as.bool_value ? "#t" : "#f", stdout); break;
    case AST_FIXNUM: printf("%lld", (long long)node->as.fixnum); break;
    case AST_SYMBOL: fputs(node->as.symbol, stdout); break;
    case AST_STRING: print_string(node->as.string); break;
    case AST_PAIR: print_pair(node); break;
    }
}

void ast_free_all(void)
{
    ArenaBlock *block = arena;
    while (block) {
        for (size_t i = 0; i < block->used; i++) {
            if (block->nodes[i].tag == AST_STRING) {
                free(block->nodes[i].as.string);
            }
        }
        ArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    arena = NULL;

    for (size_t i = 0; i < SYMBOL_TABLE_SIZE; i++) {
        free(symbols[i]);
        symbols[i] = NULL;
    }
}
