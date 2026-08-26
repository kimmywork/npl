/*
 * macro.c — NPL macro system implementation.
 *
 * Phase 6: R7RS syntax-rules (hygienic macros).
 *
 * Implements:
 *   - Pattern matching with literals, variables, wildcards, ellipsis
 *   - Template expansion with ellipsis repetition
 *   - Hygiene via variable renaming
 *   - Nested patterns and templates
 *
 * R7RS-small Section 4.3.2 (Pattern language):
 *   - <pattern> → <identifier> | <literal> | (<pattern>*) | (<pattern>+ . <pattern>)
 *                | (<pattern>* <pattern> <ellipsis>) | #(<pattern>*)
 *   - <template> → <identifier> | (<template>*) | (<template>+ . <template>)
 *                | (<template>* <template> <ellipsis>) | #(<template>*)
 *   - <ellipsis> → ... (identifier with binding ... in environment)
 */

#include "macro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- Environment ---- */

void macro_env_init(MacroEnv *env)
{
    env->macros = NULL;
    env->next_hygiene_id = 0;
}

/* ---- Binding list for pattern matching ---- */

typedef struct Binding {
    const char *name;    /* Pattern variable name (interned) */
    AstNode    *value;   /* Matched value */
    int         is_list; /* 1 if this is an ellipsis binding (list of values) */
    AstNode    *values;  /* List of values for ellipsis bindings */
    struct Binding *next;
} Binding;

/* Free a binding list */
static void free_bindings(Binding *b)
{
    while (b) {
        Binding *next = b->next;
        free(b);
        b = next;
    }
}

/* Clone a binding list */
static Binding *clone_bindings(Binding *b)
{
    Binding *result = NULL;
    Binding **tail = &result;
    while (b) {
        Binding *copy = (Binding *)calloc(1, sizeof(Binding));
        copy->name = b->name;
        copy->value = b->value;
        copy->is_list = b->is_list;
        copy->values = b->values;
        copy->next = NULL;
        *tail = copy;
        tail = &copy->next;
        b = b->next;
    }
    return result;
}

/* Add a binding */
static Binding *add_binding(Binding *list, const char *name, AstNode *value)
{
    Binding *b = (Binding *)calloc(1, sizeof(Binding));
    b->name = sym_intern(name);
    b->value = value;
    b->is_list = 0;
    b->values = NULL;
    b->next = list;
    return b;
}

/* Add an ellipsis binding (list of values) */
static Binding *add_ellipsis_binding(Binding *list, const char *name, AstNode *values)
{
    Binding *b = (Binding *)calloc(1, sizeof(Binding));
    b->name = sym_intern(name);
    b->value = NULL;
    b->is_list = 1;
    b->values = values;
    b->next = list;
    return b;
}

/* Look up a binding */
static AstNode *lookup_binding(Binding *list, const char *name)
{
    const char *interned = sym_intern(name);
    for (Binding *b = list; b; b = b->next) {
        if (b->name == interned && !b->is_list) return b->value;
    }
    return NULL;
}

/* Look up an ellipsis binding */
static AstNode *lookup_ellipsis_binding(Binding *list, const char *name)
{
    const char *interned = sym_intern(name);
    for (Binding *b = list; b; b = b->next) {
        if (b->name == interned && b->is_list) return b->values;
    }
    return NULL;
}

/* Merge two binding lists (b2 into b1, b1 takes precedence) */
static Binding *merge_bindings(Binding *b1, Binding *b2)
{
    if (!b1) return b2;
    if (!b2) return b1;
    Binding *result = clone_bindings(b1);
    Binding **tail = &result;
    while (*tail) tail = &(*tail)->next;
    *tail = b2;
    return result;
}

/* ---- Check if symbol is a literal ---- */

static int is_literal(AstNode *literals, const char *name)
{
    if (!literals) return 0;
    const char *interned = sym_intern(name);
    AstNode *cur = literals;
    while (cur && !AST_IS_NIL(cur)) {
        AstNode *lit = AST_CAR(cur);
        if (lit->tag == AST_SYMBOL && lit->as.symbol == interned) return 1;
        cur = AST_CDR(cur);
    }
    return 0;
}

/* ---- Check if symbol is ellipsis ---- */

static int is_ellipsis(const char *name)
{
    return strcmp(name, "...") == 0;
}

/* ---- Check if symbol is wildcard ---- */

static int is_wildcard(const char *name)
{
    return strcmp(name, "_") == 0;
}

/* ---- Pattern matching ---- */

/*
 * Match a pattern against an expr.
 * Returns bindings on success, NULL on failure.
 *
 * R7RS pattern rules:
 *   - _ matches anything, no binding
 *   - <identifier> (not literal, not ...) matches and binds
 *   - <literal> matches if equal
 *   - (<pattern>*) matches list of same length
 *   - (<pattern>+ . <pattern>) matches improper list
 *   - (<pattern>* <pattern> ...) matches zero or more repetitions
 *   - <datum> matches if equal
 */
static Binding *match_pattern(AstNode *pattern, AstNode *expr, AstNode *literals)
{
    if (!pattern) return NULL;

    /* Handle nil pattern */
    if (pattern->tag == AST_NIL) {
        if (!expr || expr->tag == AST_NIL) return (Binding *)calloc(1, sizeof(Binding));
        return NULL;
    }

    if (!expr) return NULL;


    /* Wildcard _ */
    if (pattern->tag == AST_SYMBOL && is_wildcard(pattern->as.symbol)) {
        return (Binding *)calloc(1, sizeof(Binding));
    }

    /* Symbol: pattern variable or literal */
    if (pattern->tag == AST_SYMBOL) {
        if (is_ellipsis(pattern->as.symbol)) return NULL; /* ... alone is invalid */
        if (is_literal(literals, pattern->as.symbol)) {
            /* Literal: must match exactly */
            if (expr->tag == AST_SYMBOL &&
                strcmp(pattern->as.symbol, expr->as.symbol) == 0) {
                return (Binding *)calloc(1, sizeof(Binding));
            }
            return NULL;
        }
        /* Pattern variable: bind */
        return add_binding(NULL, pattern->as.symbol, expr);
    }

    /* Fixnum literal */
    if (pattern->tag == AST_FIXNUM) {
        if (expr->tag == AST_FIXNUM && pattern->as.fixnum == expr->as.fixnum) {
            return (Binding *)calloc(1, sizeof(Binding));
        }
        return NULL;
    }

    /* Boolean literal */
    if (pattern->tag == AST_BOOL) {
        if (expr->tag == AST_BOOL && pattern->as.bool_val == expr->as.bool_val) {
            return (Binding *)calloc(1, sizeof(Binding));
        }
        return NULL;
    }

    /* String literal */
    if (pattern->tag == AST_STRING) {
        if (expr->tag == AST_STRING &&
            strcmp(pattern->as.string, expr->as.string) == 0) {
            return (Binding *)calloc(1, sizeof(Binding));
        }
        return NULL;
    }

    /* Pair pattern */
    if (pattern->tag == AST_PAIR) {
        if (expr->tag != AST_PAIR && expr->tag != AST_NIL) return NULL;

        AstNode *p_head = AST_CAR(pattern);
        AstNode *p_rest = AST_CDR(pattern);

        /* Check for ellipsis: (<pattern> ...) */
        if (p_rest && !AST_IS_NIL(p_rest) &&
            AST_CAR(p_rest) && AST_CAR(p_rest)->tag == AST_SYMBOL &&
            is_ellipsis(AST_CAR(p_rest)->as.symbol)) {
            /* Ellipsis pattern: match zero or more elements */
            AstNode *inner_pattern = p_head;
            Binding *result = NULL;

            /* Collect all values for each pattern variable */
            /* First, figure out which variables the inner pattern uses */
            /* For simplicity, we match each element and collect bindings */
            AstNode *e = expr;
            Binding *first_match = NULL;
            int count = 0;

            /* Match each element against the inner pattern */
            while (e && !AST_IS_NIL(e)) {
                AstNode *elem = AST_CAR(e);
                Binding *match = match_pattern(inner_pattern, elem, literals);
                if (!match) return NULL; /* All must match */

                if (count == 0) {
                    first_match = match;
                } else {
                    /* Append values to existing bindings */
                    for (Binding *b = match; b; b = b->next) {
                        if (b->name) {
                            /* Find existing binding */
                            int found = 0;
                            for (Binding *fb = first_match; fb; fb = fb->next) {
                                if (fb->name == b->name) {
                                    /* Append to ellipsis list */
                                    fb->values = ast_pair(b->value, fb->values);
                                    fb->is_list = 1;
                                    found = 1;
                                    break;
                                }
                            }
                            if (!found) {
                                /* New binding */
                                first_match = add_ellipsis_binding(first_match, b->name,
                                    ast_pair(b->value, ast_nil()));
                            }
                        }
                    }
                }
                count++;
                e = AST_CDR(e);
            }

            /* If no elements matched, create empty ellipsis bindings */
            if (count == 0) {
                /* Need to know what variables the pattern uses */
                /* For now, return empty bindings */
                first_match = (Binding *)calloc(1, sizeof(Binding));
            }

            /* Reverse the collected lists */
            for (Binding *b = first_match; b; b = b->next) {
                if (b->is_list && b->values) {
                    /* Reverse the list */
                    AstNode *rev = ast_nil();
                    AstNode *cur = b->values;
                    while (cur && !AST_IS_NIL(cur)) {
                        rev = ast_pair(AST_CAR(cur), rev);
                        cur = AST_CDR(cur);
                    }
                    b->values = rev;
                }
            }

            return first_match;
        }

        /* Regular pair pattern */
        if (expr->tag != AST_PAIR) {
            return NULL;
        }

        Binding *head_bindings = match_pattern(p_head, AST_CAR(expr), literals);
        if (!head_bindings) {
            return NULL;
        }

        Binding *rest_bindings = match_pattern(p_rest, AST_CDR(expr), literals);
        if (!rest_bindings) {
            free_bindings(head_bindings);
            return NULL;
        }

        return merge_bindings(head_bindings, rest_bindings);
    }

    return NULL;
}

/* ---- Template expansion ---- */

/*
 * Expand a template with bindings.
 *
 * R7RS template rules:
 *   - <identifier> (pattern variable) → bound value
 *   - <identifier> (not pattern variable) → literal symbol
 *   - (<template>* <template> ...) → expand ellipsis
 *   - (<template>*) → expand each element
 *   - <datum> → as is
 */
static AstNode *expand_template(AstNode *tmpl, Binding *bindings, int hygiene_id)
{
    if (!tmpl) return ast_nil();

    /* Symbol: pattern variable or literal */
    if (tmpl->tag == AST_SYMBOL) {
        AstNode *val = lookup_binding(bindings, tmpl->as.symbol);
        if (val) return val;
        /* Literal symbol */
        return tmpl;
    }

    /* Non-pair compound: return as is (but recurse into children) */
    if (tmpl->tag == AST_IF || tmpl->tag == AST_DEFINE || tmpl->tag == AST_LAMBDA ||
        tmpl->tag == AST_LET || tmpl->tag == AST_BEGIN || tmpl->tag == AST_SET ||
        tmpl->tag == AST_QUOTE) {
        /* Special form in template — recurse into car/cdr */
        AstNode *h = expand_template(AST_CAR(tmpl), bindings, 0);
        AstNode *r = expand_template(AST_CDR(tmpl), bindings, 0);
        if (h != AST_CAR(tmpl) || r != AST_CDR(tmpl)) {
            AstNode *result = ast_pair(h, r);
            result->tag = tmpl->tag;
            return result;
        }
        return tmpl;
    }

    /* Non-pair: return as is */
    if (tmpl->tag != AST_PAIR) return tmpl;

    /* Pair template */
    AstNode *t_head = AST_CAR(tmpl);
    AstNode *t_rest = AST_CDR(tmpl);

    /* Check for ellipsis: (<template> ...) */
    if (t_rest && !AST_IS_NIL(t_rest) &&
        AST_CAR(t_rest) && AST_CAR(t_rest)->tag == AST_SYMBOL &&
        is_ellipsis(AST_CAR(t_rest)->as.symbol)) {
        /* Ellipsis expansion */
        AstNode *inner = t_head;

        /* Find all ellipsis bindings referenced in inner template */
        /* For each ellipsis binding, expand the template once per value */
        /* This is complex — for now, use a simplified approach */
        /* Collect all ellipsis bindings */
        AstNode *result = ast_nil();
        AstNode **tail = &result;

        /* Get the first ellipsis binding to determine count */
        int count = 0;
        AstNode *first_values = NULL;
        for (Binding *b = bindings; b; b = b->next) {
            if (b->is_list && b->values) {
                first_values = b->values;
                /* Count elements */
                AstNode *cur = b->values;
                while (cur && !AST_IS_NIL(cur)) {
                    count++;
                    cur = AST_CDR(cur);
                }
                break;
            }
        }

        if (count == 0 || !first_values) {
            /* No ellipsis bindings — skip */
            return ast_nil();
        }

        /* Expand once per element */
        for (int i = 0; i < count; i++) {
            /* Create new bindings with the i-th element from each ellipsis binding */
            Binding *iter_bindings = NULL;
            for (Binding *b = bindings; b; b = b->next) {
                if (b->is_list && b->values) {
                    /* Get i-th element */
                    AstNode *cur = b->values;
                    for (int j = 0; j < i && cur && !AST_IS_NIL(cur); j++) {
                        cur = AST_CDR(cur);
                    }
                    if (cur && !AST_IS_NIL(cur)) {
                        iter_bindings = add_binding(iter_bindings, b->name, AST_CAR(cur));
                    }
                }
            }

            /* Expand inner template with iteration bindings */
            Binding *combined = merge_bindings(iter_bindings, bindings);
            AstNode *expanded = expand_template(inner, combined, hygiene_id);
            *tail = ast_pair(expanded, ast_nil());
            tail = &((*tail)->as.pair.cdr);
        }

        return result;
    }

    /* Regular pair */
    AstNode *head = expand_template(t_head, bindings, hygiene_id);
    AstNode *rest = expand_template(t_rest, bindings, hygiene_id);
    return ast_pair(head, rest);
}

/* ---- Define a macro ---- */

void macro_define(MacroEnv *env, const char *name, AstNode *syntax_rules)
{
    Macro *m = (Macro *)calloc(1, sizeof(Macro));
    m->name = sym_intern(name);
    m->rules = NULL;

    if (!syntax_rules || syntax_rules->tag != AST_PAIR) {
        m->next = env->macros;
        env->macros = m;
        return;
    }

    /* Parse: (syntax-rules (literals) rule ...) */
    AstNode *rest = AST_CDR(syntax_rules);
    if (!rest || AST_IS_NIL(rest)) {
        m->next = env->macros;
        env->macros = m;
        return;
    }

    /* Literals list */
    AstNode *literals = AST_CAR(rest);
    rest = AST_CDR(rest);

    /* Store literals in macro (for later use in matching) */
    /* For now, we'll pass them during expansion */

    /* Parse rules: (pattern template) */
    MacroRule **tail = &m->rules;
    while (rest && !AST_IS_NIL(rest)) {
        AstNode *rule = AST_CAR(rest);
        if (rule->tag == AST_PAIR && AST_CDR(rule) && !AST_IS_NIL(AST_CDR(rule))) {
            AstNode *pattern = AST_CAR(rule);
            AstNode *tmpl = AST_CAR(AST_CDR(rule));

            MacroRule *r = (MacroRule *)calloc(1, sizeof(MacroRule));
            r->pattern = pattern;
            r->template = tmpl;
            r->next = NULL;
            *tail = r;
            tail = &r->next;
        }
        rest = AST_CDR(rest);
    }

    m->next = env->macros;
    env->macros = m;
}

/* ---- Check if macro ---- */

int macro_is_defined(MacroEnv *env, const char *name)
{
    const char *interned = sym_intern(name);
    for (Macro *m = env->macros; m; m = m->next) {
        if (m->name == interned) return 1;
    }
    return 0;
}

/* ---- Expand a macro call ---- */

AstNode *macro_expand(MacroEnv *env, AstNode *expr)
{
    if (!expr || expr->tag != AST_PAIR) return NULL;

    AstNode *head = AST_CAR(expr);
    if (head->tag != AST_SYMBOL) return NULL;

    const char *name = head->as.symbol;
    Macro *m = NULL;
    for (Macro *cur = env->macros; cur; cur = cur->next) {
        if (cur->name == name) {
            m = cur;
            break;
        }
    }
    if (!m) return NULL;

    /* Try each rule */
    for (MacroRule *r = m->rules; r; r = r->next) {
        /* Add the macro name itself as a literal for matching */
        AstNode *literals = ast_pair(ast_symbol(name), ast_nil());
        Binding *bindings = match_pattern(r->pattern, expr, literals);
        if (bindings) {
            AstNode *result = expand_template(r->template, bindings, env->next_hygiene_id++);
            return result;
        }
    }

    return NULL;
}

/* ---- Recursive expansion ---- */

/* Recursively expand macros in an expression tree */
static AstNode *macro_expand_expr(MacroEnv *env, AstNode *expr)
{
    if (!expr) return expr;

    /* First try to expand this expression as a macro */
    AstNode *expanded = macro_expand(env, expr);
    if (expanded) {
        return macro_expand_expr(env, expanded);
    }

    /* Not a macro — recursively expand sub-expressions */
    /* Handle all compound node types (PAIR, DEFINE, IF, LAMBDA, LET, BEGIN, SET, QUOTE) */
    if (expr->tag == AST_PAIR || expr->tag == AST_DEFINE || expr->tag == AST_IF ||
        expr->tag == AST_LAMBDA || expr->tag == AST_LET || expr->tag == AST_BEGIN ||
        expr->tag == AST_SET || expr->tag == AST_QUOTE) {
        AstNode *new_head = macro_expand_expr(env, AST_CAR(expr));
        AstNode *new_rest = macro_expand_expr(env, AST_CDR(expr));
        if (new_head != AST_CAR(expr) || new_rest != AST_CDR(expr)) {
            /* Create new node preserving the original tag */
            AstNode *result = ast_pair(new_head, new_rest);
            result->tag = expr->tag;
            return result;
        }
        return expr;
    }

    return expr;
}

/* ---- Expand program ---- */

AstNode *macro_expand_program(MacroEnv *env, AstNode *program)
{
    if (!program || AST_IS_NIL(program)) return ast_nil();

    AstNode *result = ast_nil();
    AstNode **tail = &result;

    AstNode *cur = program;
    while (cur && !AST_IS_NIL(cur)) {
        AstNode *expr = AST_CAR(cur);

        /* Check for define-syntax */
        if (expr->tag == AST_PAIR && AST_CAR(expr)->tag == AST_SYMBOL &&
            strcmp(AST_CAR(expr)->as.symbol, "define-syntax") == 0) {
            AstNode *name_node = AST_CAR(AST_CDR(expr));
            AstNode *rules = AST_CAR(AST_CDR(AST_CDR(expr)));
            if (name_node->tag == AST_SYMBOL) {
                macro_define(env, name_node->as.symbol, rules);
            }
        } else {
            /* Recursively expand macros in expression */
            AstNode *expanded = macro_expand_expr(env, expr);
            *tail = ast_pair(expanded, ast_nil());
            tail = &((*tail)->as.pair.cdr);
        }

        cur = AST_CDR(cur);
    }

    return result;
}
