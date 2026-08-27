#ifndef NPL_MACRO_H
#define NPL_MACRO_H

#include "ast.h"

/*
 * NPL Macro System — Phase 6
 *
 * Implements R7RS syntax-rules (simplified).
 * Supports pattern matching and template expansion.
 *
 * Usage in NPL:
 *   (define-syntax my-if
 *     (syntax-rules ()
 *       ((my-if test conseq alt)
 *        (if test conseq alt))))
 */

/* ---- Macro rule ---- */

typedef struct MacroRule {
    AstNode *pattern;    /* Pattern to match against */
    AstNode *template;   /* Template to expand to */
    struct MacroRule *next;
} MacroRule;

/* ---- Macro definition ---- */

typedef struct Macro {
    const char *name;
    MacroRule  *rules;
    struct Macro *next;
} Macro;

/* ---- Macro environment ---- */

typedef struct {
    Macro *macros;
    int    next_hygiene_id;  /* For generating unique names */
} MacroEnv;

/* Initialize macro environment */
void macro_env_init(MacroEnv *env);

/* Register a macro: (define-syntax name (syntax-rules (literals) rule ...)) */
void macro_define(MacroEnv *env, const char *name, AstNode *syntax_rules);

/* Check if a symbol is a macro */
int macro_is_defined(MacroEnv *env, const char *name);

/* Expand a macro call. Returns the expanded AST, or NULL if not a macro call.
 * Caller must check if the result needs further expansion.
 */
AstNode *macro_expand(MacroEnv *env, AstNode *expr);

/* Expand all macros in a program (list of top-level expressions).
 * Returns a new list with all macros expanded.
 */
AstNode *macro_expand_program(MacroEnv *env, AstNode *program);

#endif /* NPL_MACRO_H */
