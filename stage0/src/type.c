/*
 * type.c — NPL type system implementation.
 *
 * Phase 5: Simple type checking with annotations.
 * Supports: Integer, Float, Bool, String, Dynamic, (-> A B), type vars.
 */

#include "type.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Static type singletons ---- */

static Type g_type_dynamic  = { TYPE_DYNAMIC, {{0}} };
static Type g_type_integer  = { TYPE_INTEGER, {{0}} };
static Type g_type_float    = { TYPE_FLOAT,   {{0}} };
static Type g_type_bool     = { TYPE_BOOL,    {{0}} };
static Type g_type_string   = { TYPE_STRING,  {{0}} };
static Type g_type_void     = { TYPE_VOID,    {{0}} };

Type *type_dynamic(void)  { return &g_type_dynamic; }
Type *type_integer(void)  { return &g_type_integer; }
Type *type_float(void)    { return &g_type_float;   }
Type *type_bool(void)     { return &g_type_bool;    }
Type *type_string(void)   { return &g_type_string;  }
Type *type_void(void)     { return &g_type_void;    }

static int next_type_var_id = 0;

Type *type_var(int id)
{
    Type *t = (Type *)calloc(1, sizeof(Type));
    t->kind = TYPE_VAR;
    t->as.var.id = id;
    t->as.var.instance = NULL;
    return t;
}

Type *type_func(Type *param, Type *result)
{
    Type *t = (Type *)calloc(1, sizeof(Type));
    t->kind = TYPE_FUNC;
    t->as.func.param = param;
    t->as.func.result = result;
    return t;
}

Type *type_pair(Type *car, Type *cdr)
{
    Type *t = (Type *)calloc(1, sizeof(Type));
    t->kind = TYPE_PAIR;
    t->as.pair.car = car;
    t->as.pair.cdr = cdr;
    return t;
}

/* ---- Type comparison ---- */

/* Resolve type variables (union-find) */
static Type *resolve(Type *t)
{
    while (t && t->kind == TYPE_VAR && t->as.var.instance) {
        t = t->as.var.instance;
    }
    return t;
}

int type_equal(Type *a, Type *b)
{
    a = resolve(a);
    b = resolve(b);

    if (!a || !b) return 0;
    if (a == b) return 1;
    if (a->kind != b->kind) return 0;

    switch (a->kind) {
    case TYPE_DYNAMIC:
    case TYPE_INTEGER:
    case TYPE_FLOAT:
    case TYPE_BOOL:
    case TYPE_STRING:
    case TYPE_VOID:
        return 1;  /* Singletons of same kind are equal */
    case TYPE_VAR:
        return a->as.var.id == b->as.var.id;
    case TYPE_FUNC:
        return type_equal(a->as.func.param, b->as.func.param) &&
               type_equal(a->as.func.result, b->as.func.result);
    case TYPE_PAIR:
        return type_equal(a->as.pair.car, b->as.pair.car) &&
               type_equal(a->as.pair.cdr, b->as.pair.cdr);
    }
    return 0;
}

int type_is_numeric(Type *t)
{
    t = resolve(t);
    return t && (t->kind == TYPE_INTEGER || t->kind == TYPE_FLOAT);
}

/* ---- Type environment ---- */

void type_env_init(TypeEnv *env)
{
    memset(env, 0, sizeof(TypeEnv));
}

void type_env_bind(TypeEnv *env, const char *name, Type *type)
{
    /* Check if already bound (update) */
    for (int i = 0; i < env->count; i++) {
        if (env->names[i] == name || strcmp(env->names[i], name) == 0) {
            env->types[i] = type;
            return;
        }
    }
    /* New binding */
    if (env->count < TYPE_ENV_MAX) {
        env->names[env->count] = sym_intern(name);
        env->types[env->count] = type;
        env->count++;
    }
}

Type *type_env_lookup(TypeEnv *env, const char *name)
{
    const char *interned = sym_intern(name);
    for (int i = env->count - 1; i >= 0; i--) {
        if (env->names[i] == interned) {
            return env->types[i];
        }
    }
    return NULL;
}

/* ---- Annotation parsing ---- */

Type *parse_type_annotation(AstNode *node)
{
    if (!node) return type_dynamic();

    switch (node->tag) {
    case AST_SYMBOL: {
        const char *name = node->as.symbol;
        if (strcmp(name, "Integer") == 0) return type_integer();
        if (strcmp(name, "Int") == 0)     return type_integer();
        if (strcmp(name, "Float") == 0)   return type_float();
        if (strcmp(name, "Double") == 0)  return type_float();
        if (strcmp(name, "Bool") == 0)    return type_bool();
        if (strcmp(name, "String") == 0)  return type_string();
        if (strcmp(name, "Dynamic") == 0) return type_dynamic();
        if (strcmp(name, "Void") == 0)    return type_void();
        return type_dynamic();  /* Unknown type → Dynamic */
    }
    case AST_PAIR: {
        /* Could be (-> A B) or (Pair A B) */
        AstNode *head = AST_CAR(node);
        if (head->tag == AST_SYMBOL) {
            if (strcmp(head->as.symbol, "->") == 0) {
                /* Function type: (-> param result) */
                AstNode *args = AST_CDR(node);
                if (args && !AST_IS_NIL(args)) {
                    Type *param = parse_type_annotation(AST_CAR(args));
                    AstNode *rest = AST_CDR(args);
                    if (rest && !AST_IS_NIL(rest)) {
                        Type *result = parse_type_annotation(AST_CAR(rest));
                        return type_func(param, result);
                    }
                }
            }
            if (strcmp(head->as.symbol, "Pair") == 0) {
                AstNode *args = AST_CDR(node);
                if (args && !AST_IS_NIL(args)) {
                    Type *car = parse_type_annotation(AST_CAR(args));
                    AstNode *rest = AST_CDR(args);
                    if (rest && !AST_IS_NIL(rest)) {
                        Type *cdr = parse_type_annotation(AST_CAR(rest));
                        return type_pair(car, cdr);
                    }
                }
            }
        }
        return type_dynamic();
    }
    default:
        return type_dynamic();
    }
}

/* ---- Type inference ---- */

Type *type_infer(AstNode *expr, TypeEnv *env, char *errbuf, size_t errbuf_size)
{
    if (!expr) return type_dynamic();

    switch (expr->tag) {
    case AST_FIXNUM:
        return type_integer();

    case AST_FLONUM:
        return type_float();

    case AST_BOOL:
        return type_bool();

    case AST_STRING:
        return type_string();

    case AST_NIL:
        return type_dynamic();

    case AST_SYMBOL: {
        Type *t = type_env_lookup(env, expr->as.symbol);
        return t ? t : type_dynamic();
    }

    case AST_IF: {
        /* if test conseq alt */
        AstNode *conseq = AST_CAR(AST_CDR(expr));
        return type_infer(conseq, env, errbuf, errbuf_size);
    }

    case AST_LET: {
        /* let ((name val) ...) body */
        AstNode *bindings = AST_CAR(expr);
        AstNode *body = AST_CDR(expr);

        /* Process bindings */
        while (bindings && !AST_IS_NIL(bindings)) {
            AstNode *binding = AST_CAR(bindings);
            AstNode *name = AST_CAR(binding);
            AstNode *val = AST_CAR(AST_CDR(binding));

            Type *val_type = type_infer(val, env, errbuf, errbuf_size);
            if (name->tag == AST_SYMBOL) {
                type_env_bind(env, name->as.symbol, val_type);
            }
            bindings = AST_CDR(bindings);
        }

        /* Infer body type */
        if (body && !AST_IS_NIL(body)) {
            return type_infer(body, env, errbuf, errbuf_size);
        }
        return type_dynamic();
    }

    case AST_PAIR: {
        /* Function application: (f arg1 arg2 ...) */
        AstNode *func = AST_CAR(expr);
        AstNode *args = AST_CDR(expr);

        /* Get function type */
        Type *func_type = type_infer(func, env, errbuf, errbuf_size);
        func_type = resolve(func_type);

        if (func_type && func_type->kind == TYPE_FUNC) {
            return func_type->as.func.result;
        }

        /* Check for built-in arithmetic */
        if (func->tag == AST_SYMBOL) {
            const char *name = func->as.symbol;
            if (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
                strcmp(name, "*") == 0 || strcmp(name, "/") == 0 ||
                strcmp(name, "%") == 0) {
                /* Infer from arguments */
                if (args && !AST_IS_NIL(args)) {
                    Type *arg_type = type_infer(AST_CAR(args), env, errbuf, errbuf_size);
                    if (arg_type && arg_type->kind == TYPE_FLOAT) return type_float();
                    return type_integer();
                }
                return type_integer();
            }
            if (strcmp(name, "<") == 0 || strcmp(name, ">") == 0 ||
                strcmp(name, "=") == 0 || strcmp(name, "<=") == 0 ||
                strcmp(name, ">=") == 0 || strcmp(name, "not=") == 0) {
                return type_bool();
            }
            if (strcmp(name, "cons") == 0) {
                return type_pair(
                    args && !AST_IS_NIL(args) ? type_infer(AST_CAR(args), env, errbuf, errbuf_size) : type_dynamic(),
                    args && AST_CDR(args) && !AST_IS_NIL(AST_CDR(args)) ? type_infer(AST_CAR(AST_CDR(args)), env, errbuf, errbuf_size) : type_dynamic()
                );
            }
            if (strcmp(name, "car") == 0) {
                Type *pair_type = args && !AST_IS_NIL(args) ? type_infer(AST_CAR(args), env, errbuf, errbuf_size) : type_dynamic();
                pair_type = resolve(pair_type);
                if (pair_type && pair_type->kind == TYPE_PAIR) return pair_type->as.pair.car;
                return type_dynamic();
            }
            if (strcmp(name, "cdr") == 0) {
                Type *pair_type = args && !AST_IS_NIL(args) ? type_infer(AST_CAR(args), env, errbuf, errbuf_size) : type_dynamic();
                pair_type = resolve(pair_type);
                if (pair_type && pair_type->kind == TYPE_PAIR) return pair_type->as.pair.cdr;
                return type_dynamic();
            }
            if (strcmp(name, "null?") == 0) return type_bool();
        }

        return type_dynamic();
    }

    case AST_DEFINE: {
        /* (define name value) or (define (name params...) body) */
        AstNode *name_or_sig = AST_CAR(expr);
        AstNode *body = AST_CDR(expr);

        if (name_or_sig->tag == AST_SYMBOL) {
            /* Simple define */
            Type *val_type = type_infer(body, env, errbuf, errbuf_size);
            type_env_bind(env, name_or_sig->as.symbol, val_type);
            return type_void();
        }
        return type_dynamic();
    }

    case AST_LAMBDA:
    case AST_BEGIN:
    case AST_QUOTE:
    case AST_SET:
    default:
        return type_dynamic();
    }
}

/* ---- Type checking ---- */

int type_check(AstNode *expr, Type *expected, TypeEnv *env, char *errbuf, size_t errbuf_size)
{
    Type *inferred = type_infer(expr, env, errbuf, errbuf_size);
    inferred = resolve(inferred);
    expected = resolve(expected);

    if (!inferred || !expected) return 1;  /* Can't check — assume ok */

    /* Dynamic is compatible with everything */
    if (inferred->kind == TYPE_DYNAMIC || expected->kind == TYPE_DYNAMIC) return 1;

    if (!type_equal(inferred, expected)) {
        if (errbuf && errbuf_size > 0) {
            snprintf(errbuf, errbuf_size, "type mismatch: expected different type");
        }
        return 0;
    }
    return 1;
}

/* ---- QBE type mapping ---- */

const char *qbe_type_for(Type *type)
{
    type = resolve(type);
    if (!type) return "l";

    switch (type->kind) {
    case TYPE_INTEGER:
    case TYPE_BOOL:
        return "w";
    case TYPE_FLOAT:
        return "d";
    case TYPE_DYNAMIC:
    case TYPE_STRING:
    case TYPE_PAIR:
    case TYPE_FUNC:
    case TYPE_VAR:
    case TYPE_VOID:
    default:
        return "l";
    }
}

/* ---- Guard check ---- */

int type_needs_guard(Type *from, Type *to)
{
    from = resolve(from);
    to = resolve(to);

    if (!from || !to) return 0;

    /* Dynamic ↔ typed boundary needs guard */
    if (from->kind == TYPE_DYNAMIC && to->kind != TYPE_DYNAMIC) return 1;
    if (from->kind != TYPE_DYNAMIC && to->kind == TYPE_DYNAMIC) return 1;

    /* Different concrete types need guard */
    if (from->kind != to->kind) return 1;

    return 0;
}
