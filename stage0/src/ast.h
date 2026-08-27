#ifndef NPL_AST_H
#define NPL_AST_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AST_NIL,
    AST_BOOL,
    AST_FIXNUM,
    AST_SYMBOL,
    AST_STRING,
    AST_PAIR
} AstTag;

typedef struct AstNode AstNode;

struct AstNode {
    AstTag tag;
    union {
        int bool_value;
        int64_t fixnum;
        const char *symbol;
        char *string;
        struct {
            AstNode *car;
            AstNode *cdr;
        } pair;
    } as;
};

AstNode *ast_nil(void);
AstNode *ast_bool(int value);
AstNode *ast_fixnum(int64_t value);
AstNode *ast_symbol(const char *name);
AstNode *ast_string(char *value);
AstNode *ast_pair(AstNode *car, AstNode *cdr);

int ast_is_nil(const AstNode *node);
int ast_is_pair(const AstNode *node);
void ast_print(const AstNode *node);
void ast_free_all(void);

#define AST_CAR(node) ((node)->as.pair.car)
#define AST_CDR(node) ((node)->as.pair.cdr)

#endif
