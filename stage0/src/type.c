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
#include <stdarg.h>

/* ---- Static type singletons ---- */

static Type g_type_dynamic  = { TYPE_DYNAMIC, {{0}} };
static Type g_type_integer  = { TYPE_INTEGER, {{0}} };
static Type g_type_float    = { TYPE_FLOAT,   {{0}} };
static Type g_type_bool     = { TYPE_BOOL,    {{0}} };
static Type g_type_string   = { TYPE_STRING,  {{0}} };
static Type g_type_void     = { TYPE_VOID,    {{0}} };
static int next_type_var_id = 0;

Type *type_dynamic(void)  { return &g_type_dynamic; }
Type *type_integer(void)  { return &g_type_integer; }
Type *type_float(void)    { return &g_type_float;   }
Type *type_bool(void)     { return &g_type_bool;    }
Type *type_string(void)   { return &g_type_string;  }
Type *type_void(void)     { return &g_type_void;    }

Type *type_var(int id)
{
    Type *t = (Type *)calloc(1, sizeof(Type));
    t->kind = TYPE_VAR;
    t->as.var.id = id;
    t->as.var.instance = NULL;
    return t;
}

Type *type_fresh_var(void) { return type_var(next_type_var_id++); }

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

Type *type_resolve(Type *t) { return resolve(t); }

static int occurs(Type *variable, Type *type)
{
    type = resolve(type);
    if (variable == type) return 1;
    if (!type) return 0;
    if (type->kind == TYPE_FUNC)
        return occurs(variable, type->as.func.param) ||
               occurs(variable, type->as.func.result);
    if (type->kind == TYPE_PAIR)
        return occurs(variable, type->as.pair.car) ||
               occurs(variable, type->as.pair.cdr);
    return 0;
}

int type_unify(Type *a, Type *b)
{
    a = resolve(a);
    b = resolve(b);
    if (!a || !b || a == b) return 1;
    if (a->kind == TYPE_DYNAMIC || b->kind == TYPE_DYNAMIC) return 1;
    if (a->kind == TYPE_VAR) {
        if (occurs(a, b)) return 0;
        a->as.var.instance = b;
        return 1;
    }
    if (b->kind == TYPE_VAR) return type_unify(b, a);
    if (a->kind != b->kind) return 0;
    if (a->kind == TYPE_FUNC)
        return type_unify(a->as.func.param, b->as.func.param) &&
               type_unify(a->as.func.result, b->as.func.result);
    if (a->kind == TYPE_PAIR)
        return type_unify(a->as.pair.car, b->as.pair.car) &&
               type_unify(a->as.pair.cdr, b->as.pair.cdr);
    return 1;
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

static uint64_t free_type_vars(Type *type)
{
    type = resolve(type);
    if (!type) return 0;
    if (type->kind == TYPE_VAR)
        return type->as.var.id < 64 ? UINT64_C(1) << type->as.var.id : 0;
    if (type->kind == TYPE_FUNC)
        return free_type_vars(type->as.func.param) |
               free_type_vars(type->as.func.result);
    if (type->kind == TYPE_PAIR)
        return free_type_vars(type->as.pair.car) |
               free_type_vars(type->as.pair.cdr);
    return 0;
}

static uint64_t free_env_vars(TypeEnv *env)
{
    uint64_t vars = 0;
    for (int i = 0; i < env->count; i++)
        vars |= free_type_vars(env->types[i]) & ~env->quantified[i];
    return vars;
}

static Type *instantiate_type(Type *type, uint64_t quantified,
                              Type **mapping)
{
    type = resolve(type);
    if (!type) return NULL;
    if (type->kind == TYPE_VAR && type->as.var.id < 64 &&
        (quantified & (UINT64_C(1) << type->as.var.id))) {
        if (!mapping[type->as.var.id])
            mapping[type->as.var.id] = type_fresh_var();
        return mapping[type->as.var.id];
    }
    if (type->kind == TYPE_FUNC)
        return type_func(instantiate_type(type->as.func.param, quantified, mapping),
                         instantiate_type(type->as.func.result, quantified, mapping));
    if (type->kind == TYPE_PAIR)
        return type_pair(instantiate_type(type->as.pair.car, quantified, mapping),
                         instantiate_type(type->as.pair.cdr, quantified, mapping));
    return type;
}

void type_env_bind(TypeEnv *env, const char *name, Type *type)
{
    if (env->count < TYPE_ENV_MAX) {
        env->names[env->count] = sym_intern(name);
        env->types[env->count] = type;
        env->quantified[env->count] = 0;
        env->count++;
    }
}

void type_env_bind_generalized(TypeEnv *env, const char *name, Type *type)
{
    if (env->count < TYPE_ENV_MAX) {
        env->names[env->count] = sym_intern(name);
        env->types[env->count] = type;
        env->quantified[env->count] = free_type_vars(type) & ~free_env_vars(env);
        env->count++;
    }
}

Type *type_env_lookup(TypeEnv *env, const char *name)
{
    const char *interned = sym_intern(name);
    for (int i = env->count - 1; i >= 0; i--) {
        if (env->names[i] == interned) {
            if (!env->quantified[i]) return resolve(env->types[i]);
            Type *mapping[64] = {0};
            return instantiate_type(env->types[i], env->quantified[i], mapping);
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

/* ---- Program annotation analysis ---- */

static void type_error(char *errbuf, size_t size, const char *fmt, ...)
{
    if (!errbuf || size == 0 || errbuf[0] != '\0') return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, size, fmt, ap);
    va_end(ap);
}

const char *type_name(Type *type)
{
    type = resolve(type);
    if (!type) return "<invalid>";
    switch (type->kind) {
    case TYPE_DYNAMIC: return "Dynamic";
    case TYPE_INTEGER: return "Integer";
    case TYPE_FLOAT: return "Float";
    case TYPE_BOOL: return "Bool";
    case TYPE_STRING: return "String";
    case TYPE_VOID: return "Void";
    case TYPE_VAR: return "type variable";
    case TYPE_FUNC: return "function";
    case TYPE_PAIR: return "Pair";
    }
    return "<invalid>";
}

static Type *parse_known_annotation(AstNode *node)
{
    if (!node) return NULL;
    if (node->tag == AST_SYMBOL) {
        const char *name = node->as.symbol;
        if (strcmp(name, "Integer") == 0 || strcmp(name, "Int") == 0)
            return type_integer();
        if (strcmp(name, "Float") == 0 || strcmp(name, "Double") == 0)
            return type_float();
        if (strcmp(name, "Bool") == 0) return type_bool();
        if (strcmp(name, "String") == 0) return type_string();
        if (strcmp(name, "Dynamic") == 0) return type_dynamic();
        if (strcmp(name, "Void") == 0) return type_void();
        return NULL;
    }
    if (node->tag != AST_PAIR) return NULL;

    AstNode *head = AST_CAR(node);
    AstNode *args = AST_CDR(node);
    if (!head || head->tag != AST_SYMBOL || !args || AST_IS_NIL(args))
        return NULL;
    AstNode *first = AST_CAR(args);
    AstNode *rest = AST_CDR(args);
    if (!rest || AST_IS_NIL(rest)) return NULL;
    AstNode *second = AST_CAR(rest);
    if (!AST_IS_NIL(AST_CDR(rest))) return NULL;
    Type *a = parse_known_annotation(first);
    Type *b = parse_known_annotation(second);
    if (!a || !b) return NULL;
    if (strcmp(head->as.symbol, "->") == 0) return type_func(a, b);
    if (strcmp(head->as.symbol, "Pair") == 0) return type_pair(a, b);
    return NULL;
}

void type_program_init(TypeProgram *program)
{
    memset(program, 0, sizeof(*program));
    next_type_var_id = 0;
}

TypedFunction *type_program_function(TypeProgram *program, const char *name)
{
    for (int i = program->function_count - 1; i >= 0; i--) {
        if (strcmp(program->functions[i].name, name) == 0)
            return &program->functions[i];
    }
    return NULL;
}

TypedVariable *type_program_variable(TypeProgram *program, const char *name)
{
    for (int i = program->variable_count - 1; i >= 0; i--) {
        if (strcmp(program->variables[i].name, name) == 0)
            return &program->variables[i];
    }
    return NULL;
}

TypedLambda *type_program_lambda(TypeProgram *program, AstNode *node)
{
    for (int i = program->lambda_count - 1; i >= 0; i--) {
        if (program->lambdas[i].node == node) return &program->lambdas[i];
    }
    return NULL;
}

static int parse_parameter(AstNode **slot, const char *function_name,
                           Type **type, unsigned char *annotated,
                           char *errbuf, size_t errbuf_size)
{
    AstNode *parameter = *slot;
    if (parameter && parameter->tag == AST_SYMBOL) {
        *type = type_dynamic();
        *annotated = 0;
        return 1;
    }
    if (!parameter || parameter->tag != AST_PAIR) {
        type_error(errbuf, errbuf_size, "type error in %s: malformed parameter",
                   function_name);
        return 0;
    }

    AstNode *name = AST_CAR(parameter);
    AstNode *rest = AST_CDR(parameter);
    if (!name || name->tag != AST_SYMBOL || !rest || AST_IS_NIL(rest) ||
        AST_CAR(rest)->tag != AST_SYMBOL ||
        strcmp(AST_CAR(rest)->as.symbol, ":") != 0) {
        type_error(errbuf, errbuf_size,
                   "type error in %s: expected parameter (name : Type)",
                   function_name);
        return 0;
    }
    rest = AST_CDR(rest);
    if (!rest || AST_IS_NIL(rest) || !AST_IS_NIL(AST_CDR(rest))) {
        type_error(errbuf, errbuf_size,
                   "type error in %s: expected parameter (name : Type)",
                   function_name);
        return 0;
    }
    Type *parsed = parse_known_annotation(AST_CAR(rest));
    if (!parsed) {
        type_error(errbuf, errbuf_size,
                   "type error in %s: unknown parameter annotation",
                   function_name);
        return 0;
    }
    *slot = name;
    *type = parsed;
    *annotated = 1;
    return 1;
}

static int register_definitions(AstNode *ast, TypeProgram *program,
                                char *errbuf, size_t errbuf_size)
{
    for (AstNode *forms = ast; forms && !AST_IS_NIL(forms);
         forms = AST_CDR(forms)) {
        AstNode *form = AST_CAR(forms);
        if (!form || form->tag != AST_DEFINE) continue;

        AstNode *target = AST_CAR(form);
        AstNode *annotation = NULL;
        if (target && target->tag == AST_ANNOTATION) {
            annotation = AST_CDR(target);
            target = AST_CAR(target);
            AST_CAR(form) = target;
        }

        if (target && target->tag == AST_PAIR) {
            if (program->function_count >= TYPE_PROGRAM_MAX) {
                type_error(errbuf, errbuf_size, "type error: too many functions");
                return 0;
            }
            AstNode *name = AST_CAR(target);
            if (!name || name->tag != AST_SYMBOL) {
                type_error(errbuf, errbuf_size, "type error: malformed function name");
                return 0;
            }
            TypedFunction *function =
                &program->functions[program->function_count++];
            memset(function, 0, sizeof(*function));
            function->name = name->as.symbol;
            function->result = type_dynamic();
            function->body = AST_CDR(form);
            if (annotation) {
                function->result = parse_known_annotation(annotation);
                if (!function->result) {
                    type_error(errbuf, errbuf_size,
                               "type error in %s: unknown return annotation",
                               function->name);
                    return 0;
                }
                function->result_annotated = 1;
            }

            AstNode *params = AST_CDR(target);
            while (params && !AST_IS_NIL(params)) {
                if (function->arity >= TYPE_MAX_PARAMS) {
                    type_error(errbuf, errbuf_size,
                               "type error in %s: more than 16 parameters",
                               function->name);
                    return 0;
                }
                if (!parse_parameter(&AST_CAR(params), function->name,
                                     &function->params[function->arity],
                                     &function->param_annotated[function->arity],
                                     errbuf, errbuf_size))
                    return 0;
                function->arity++;
                params = AST_CDR(params);
            }
        } else {
            if (!target || target->tag != AST_SYMBOL) {
                type_error(errbuf, errbuf_size, "type error: malformed define");
                return 0;
            }
            if (program->variable_count >= TYPE_PROGRAM_MAX) {
                type_error(errbuf, errbuf_size, "type error: too many variables");
                return 0;
            }
            TypedVariable *variable =
                &program->variables[program->variable_count++];
            variable->name = target->as.symbol;
            variable->type = type_dynamic();
            variable->annotated = annotation != NULL;
            variable->value = AST_CDR(form);
            if (annotation) {
                variable->type = parse_known_annotation(annotation);
                if (!variable->type) {
                    type_error(errbuf, errbuf_size,
                               "type error in %s: unknown annotation",
                               variable->name);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int register_lambdas(AstNode *node, TypeProgram *program,
                            char *errbuf, size_t errbuf_size)
{
    if (!node || AST_IS_NIL(node)) return 1;
    if (node->tag == AST_QUOTE) return 1;

    if (node->tag == AST_LAMBDA) {
        AstNode *params = AST_CAR(node);
        AstNode *result_annotation = NULL;
        if (params && params->tag == AST_ANNOTATION) {
            result_annotation = AST_CDR(params);
            params = AST_CAR(params);
            AST_CAR(node) = params;
        }

        if (program->lambda_count >= TYPE_PROGRAM_MAX) {
            type_error(errbuf, errbuf_size, "type error: too many lambdas");
            return 0;
        }
        TypedLambda *lambda = &program->lambdas[program->lambda_count++];
        memset(lambda, 0, sizeof(*lambda));
        lambda->node = node;
        lambda->body = AST_CDR(node);
        lambda->result = type_fresh_var();
        if (result_annotation) {
            lambda->result = parse_known_annotation(result_annotation);
            if (!lambda->result) {
                type_error(errbuf, errbuf_size,
                           "type error: unknown lambda return annotation");
                return 0;
            }
            lambda->result_annotated = 1;
        }
        for (AstNode *p = params; p && !AST_IS_NIL(p); p = AST_CDR(p)) {
            if (lambda->arity >= TYPE_MAX_PARAMS) {
                type_error(errbuf, errbuf_size,
                           "type error: lambda has more than 16 parameters");
                return 0;
            }
            if (AST_CAR(p)->tag == AST_SYMBOL) {
                lambda->params[lambda->arity] = type_fresh_var();
                lambda->param_annotated[lambda->arity] = 0;
            } else if (!parse_parameter(
                           &AST_CAR(p), "lambda",
                           &lambda->params[lambda->arity],
                           &lambda->param_annotated[lambda->arity],
                           errbuf, errbuf_size)) {
                return 0;
            }
            lambda->arity++;
        }

        for (AstNode *body = AST_CDR(node); body && !AST_IS_NIL(body);
             body = AST_CDR(body)) {
            if (!register_lambdas(AST_CAR(body), program,
                                  errbuf, errbuf_size)) return 0;
        }
        return 1;
    }

    if (AST_IS_PAIR(node)) {
        return register_lambdas(AST_CAR(node), program, errbuf, errbuf_size) &&
               register_lambdas(AST_CDR(node), program, errbuf, errbuf_size);
    }
    return 1;
}

static void mark_typed_lambdas(AstNode *node, TypeProgram *program)
{
    if (!node || AST_IS_NIL(node) || node->tag == AST_QUOTE) return;
    if (node->tag == AST_LAMBDA) {
        TypedLambda *lambda = type_program_lambda(program, node);
        if (lambda) lambda->in_typed_context = 1;
    }
    if (AST_IS_PAIR(node)) {
        mark_typed_lambdas(AST_CAR(node), program);
        mark_typed_lambdas(AST_CDR(node), program);
    }
}

static int compatible(Type *actual, Type *expected)
{
    actual = resolve(actual);
    expected = resolve(expected);
    if (!actual || !expected) return 1;
    if (actual->kind == TYPE_DYNAMIC || expected->kind == TYPE_DYNAMIC) return 1;
    return type_equal(actual, expected);
}

static Type *infer_program_expr(AstNode *expr, TypeEnv *env,
                                TypeProgram *program, int *ok,
                                char *errbuf, size_t errbuf_size);

static Type *infer_body(AstNode *body, TypeEnv *env, TypeProgram *program,
                        int *ok, char *errbuf, size_t errbuf_size)
{
    Type *result = type_void();
    for (AstNode *item = body; item && !AST_IS_NIL(item);
         item = AST_CDR(item)) {
        result = infer_program_expr(AST_CAR(item), env, program, ok,
                                    errbuf, errbuf_size);
        if (!*ok) return NULL;
    }
    return result;
}

static Type *infer_call(AstNode *expr, TypeEnv *env, TypeProgram *program,
                        int *ok, char *errbuf, size_t errbuf_size)
{
    AstNode *function = AST_CAR(expr);
    AstNode *args = AST_CDR(expr);

    Type *local_function = NULL;
    if (function && function->tag == AST_SYMBOL)
        local_function = type_env_lookup(env, function->as.symbol);
    else if (function)
        local_function = infer_program_expr(function, env, program, ok,
                                            errbuf, errbuf_size);
    Type *applied = resolve(local_function);
    if (applied && applied->kind == TYPE_FUNC) {
        int index = 0;
        for (AstNode *arg = args; arg && !AST_IS_NIL(arg);
             arg = AST_CDR(arg), index++) {
            applied = resolve(applied);
            if (!applied || applied->kind != TYPE_FUNC) {
                type_error(errbuf, errbuf_size,
                           "type error: too many arguments to local function");
                *ok = 0;
                return NULL;
            }
            Type *actual = infer_program_expr(AST_CAR(arg), env, program, ok,
                                              errbuf, errbuf_size);
            if (!type_unify(actual, applied->as.func.param)) {
                type_error(errbuf, errbuf_size,
                           "type error: local argument %d expected %s, got %s",
                           index + 1, type_name(applied->as.func.param),
                           type_name(actual));
                *ok = 0;
                return NULL;
            }
            applied = applied->as.func.result;
        }
        return applied;
    }

    if (!function || function->tag != AST_SYMBOL) return type_dynamic();
    const char *name = function->as.symbol;

    TypedFunction *callee = type_program_function(program, name);
    if (callee) {
        int index = 0;
        for (AstNode *arg = args; arg && !AST_IS_NIL(arg); arg = AST_CDR(arg)) {
            if (index >= callee->arity) break;
            Type *actual = infer_program_expr(AST_CAR(arg), env, program, ok,
                                              errbuf, errbuf_size);
            if (!*ok) return NULL;
            if (!type_unify(actual, callee->params[index])) {
                type_error(errbuf, errbuf_size,
                           "type error calling %s: argument %d expected %s, got %s",
                           name, index + 1, type_name(callee->params[index]),
                           type_name(actual));
                *ok = 0;
                return NULL;
            }
            index++;
        }
        int actual_count = 0;
        for (AstNode *arg = args; arg && !AST_IS_NIL(arg); arg = AST_CDR(arg))
            actual_count++;
        if (actual_count != callee->arity) {
            type_error(errbuf, errbuf_size,
                       "type error calling %s: expected %d arguments, got %d",
                       name, callee->arity, actual_count);
            *ok = 0;
            return NULL;
        }
        return callee->result;
    }

    if (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
        strcmp(name, "*") == 0 || strcmp(name, "/") == 0 ||
        strcmp(name, "%") == 0) {
        Type *result = NULL;
        for (AstNode *arg = args; arg && !AST_IS_NIL(arg); arg = AST_CDR(arg)) {
            Type *actual = infer_program_expr(AST_CAR(arg), env, program, ok,
                                              errbuf, errbuf_size);
            actual = resolve(actual);
            if (!*ok) return NULL;
            if (actual->kind == TYPE_DYNAMIC) return type_dynamic();
            if (actual->kind != TYPE_VAR && actual->kind != TYPE_INTEGER &&
                actual->kind != TYPE_FLOAT) {
                type_error(errbuf, errbuf_size,
                           "type error in %s: expected numeric operand, got %s",
                           name, type_name(actual));
                *ok = 0;
                return NULL;
            }
            if (!result) result = actual;
            else if (!type_unify(result, actual)) {
                type_error(errbuf, errbuf_size,
                           "type error in %s: mixed numeric operand types", name);
                *ok = 0;
                return NULL;
            }
        }
        return result ? resolve(result) : type_integer();
    }
    if (strcmp(name, "<") == 0 || strcmp(name, ">") == 0 ||
        strcmp(name, "=") == 0 || strcmp(name, "<=") == 0 ||
        strcmp(name, ">=") == 0) {
        Type *operand = NULL;
        for (AstNode *arg = args; arg && !AST_IS_NIL(arg); arg = AST_CDR(arg)) {
            Type *actual = infer_program_expr(AST_CAR(arg), env, program, ok,
                                              errbuf, errbuf_size);
            actual = resolve(actual);
            if (actual && actual->kind == TYPE_DYNAMIC) continue;
            if (!*ok || !actual ||
                (actual->kind != TYPE_VAR && actual->kind != TYPE_INTEGER &&
                 actual->kind != TYPE_FLOAT) ||
                (operand && !type_unify(operand, actual))) {
                type_error(errbuf, errbuf_size,
                           "type error in %s: expected matching numeric operands",
                           name);
                *ok = 0;
                return NULL;
            }
            if (!operand) operand = actual;
        }
        return type_bool();
    }
    if (strcmp(name, "cons") == 0) {
        Type *car = args && !AST_IS_NIL(args)
            ? infer_program_expr(AST_CAR(args), env, program, ok, errbuf, errbuf_size)
            : type_dynamic();
        AstNode *rest = args ? AST_CDR(args) : NULL;
        Type *cdr = rest && !AST_IS_NIL(rest)
            ? infer_program_expr(AST_CAR(rest), env, program, ok, errbuf, errbuf_size)
            : type_dynamic();
        return type_pair(car, cdr);
    }
    if (strcmp(name, "car") == 0 || strcmp(name, "cdr") == 0) {
        Type *pair = args && !AST_IS_NIL(args)
            ? infer_program_expr(AST_CAR(args), env, program, ok,
                                 errbuf, errbuf_size)
            : type_dynamic();
        pair = resolve(pair);
        if (pair && pair->kind == TYPE_PAIR)
            return strcmp(name, "car") == 0
                ? pair->as.pair.car : pair->as.pair.cdr;
        if (pair && pair->kind != TYPE_DYNAMIC) {
            type_error(errbuf, errbuf_size,
                       "type error in %s: expected Pair", name);
            *ok = 0;
            return NULL;
        }
        return type_dynamic();
    }
    if (strcmp(name, "null?") == 0 || strcmp(name, "pair?") == 0 ||
        strcmp(name, "number?") == 0 || strcmp(name, "not") == 0)
        return type_bool();
    return type_dynamic();
}

static Type *infer_program_expr(AstNode *expr, TypeEnv *env,
                                TypeProgram *program, int *ok,
                                char *errbuf, size_t errbuf_size)
{
    if (!expr) return type_dynamic();
    switch (expr->tag) {
    case AST_FIXNUM: return type_integer();
    case AST_FLONUM: return type_float();
    case AST_BOOL: return type_bool();
    case AST_STRING: return type_string();
    case AST_NIL: return type_dynamic();
    case AST_SYMBOL: {
        Type *local = type_env_lookup(env, expr->as.symbol);
        if (local) return local;
        TypedVariable *variable = type_program_variable(program, expr->as.symbol);
        return variable ? variable->type : type_dynamic();
    }
    case AST_PAIR:
        return infer_call(expr, env, program, ok, errbuf, errbuf_size);
    case AST_IF: {
        (void)infer_program_expr(AST_CAR(expr), env, program, ok,
                                 errbuf, errbuf_size);
        Type *a = infer_program_expr(AST_CAR(AST_CDR(expr)), env, program, ok,
                                     errbuf, errbuf_size);
        Type *b = infer_program_expr(AST_CAR(AST_CDR(AST_CDR(expr))), env,
                                     program, ok, errbuf, errbuf_size);
        if (!*ok) return NULL;
        if (!type_unify(a, b)) return type_dynamic();
        return type_resolve(a);
    }
    case AST_LET: {
        int saved = env->count;
        AstNode *bindings = AST_CAR(expr);
        for (; bindings && !AST_IS_NIL(bindings); bindings = AST_CDR(bindings)) {
            AstNode *binding = AST_CAR(bindings);
            AstNode *name = AST_CAR(binding);
            Type *value = infer_program_expr(AST_CAR(AST_CDR(binding)), env,
                                             program, ok, errbuf, errbuf_size);
            if (name && name->tag == AST_SYMBOL)
                type_env_bind_generalized(env, name->as.symbol, value);
        }
        Type *result = infer_body(AST_CDR(expr), env, program, ok,
                                  errbuf, errbuf_size);
        env->count = saved;
        return result;
    }
    case AST_BEGIN:
        return infer_body(AST_CAR(expr), env, program, ok, errbuf, errbuf_size);
    case AST_LAMBDA: {
        TypedLambda *lambda = type_program_lambda(program, expr);
        if (!lambda) return type_dynamic();
        int saved = env->count;
        AstNode *params = AST_CAR(expr);
        for (int i = 0; i < lambda->arity; i++) {
            AstNode *name = AST_CAR(params);
            type_env_bind(env, name->as.symbol, lambda->params[i]);
            params = AST_CDR(params);
        }
        Type *actual = infer_body(lambda->body, env, program, ok,
                                  errbuf, errbuf_size);
        env->count = saved;
        if (!*ok) return NULL;
        if (!type_unify(actual, lambda->result)) {
            type_error(errbuf, errbuf_size,
                       "type error in lambda: expected return %s, got %s",
                       type_name(lambda->result), type_name(actual));
            *ok = 0;
            return NULL;
        }
        lambda->result = type_resolve(lambda->result);
        Type *function_type = lambda->result;
        for (int i = lambda->arity - 1; i >= 0; i--)
            function_type = type_func(lambda->params[i], function_type);
        return function_type;
    }
    case AST_QUOTE: return type_dynamic();
    case AST_DEFINE: return type_void();
    case AST_SET: return type_dynamic();
    case AST_ANNOTATION:
        type_error(errbuf, errbuf_size, "type error: misplaced annotation");
        *ok = 0;
        return NULL;
    }
    return type_dynamic();
}

int type_analyze_program(AstNode *ast, TypeProgram *program,
                         char *errbuf, size_t errbuf_size)
{
    type_program_init(program);
    if (errbuf && errbuf_size) errbuf[0] = '\0';
    if (!register_definitions(ast, program, errbuf, errbuf_size)) return 0;
    if (!register_lambdas(ast, program, errbuf, errbuf_size)) return 0;

    TypeEnv globals;
    type_env_init(&globals);
    for (int i = 0; i < program->variable_count; i++)
        type_env_bind(&globals, program->variables[i].name,
                      program->variables[i].type);

    int ok = 1;
    for (int i = 0; i < program->variable_count && ok; i++) {
        TypedVariable *variable = &program->variables[i];
        if (variable->annotated)
            mark_typed_lambdas(variable->value, program);
        Type *actual = infer_program_expr(variable->value, &globals, program,
                                          &ok, errbuf, errbuf_size);
        if (ok && variable->annotated && !compatible(actual, variable->type)) {
            type_error(errbuf, errbuf_size,
                       "type error in %s: expected %s, got %s",
                       variable->name, type_name(variable->type),
                       type_name(actual));
            ok = 0;
        } else if (ok && !variable->annotated) {
            variable->type = actual;
            type_env_bind(&globals, variable->name, actual);
        }
    }

    for (int i = 0; i < program->function_count && ok; i++) {
        TypedFunction *function = &program->functions[i];
        int typed_context = function->result_annotated;
        for (int p = 0; p < function->arity; p++)
            if (function->param_annotated[p]) typed_context = 1;
        if (typed_context) mark_typed_lambdas(function->body, program);
        TypeEnv locals = globals;

        /* Parameter names are the normalized signature cells in program AST. */
        AstNode *definition = NULL;
        for (AstNode *forms = ast; forms && !AST_IS_NIL(forms);
             forms = AST_CDR(forms)) {
            AstNode *candidate = AST_CAR(forms);
            if (candidate && candidate->tag == AST_DEFINE &&
                AST_CAR(candidate)->tag == AST_PAIR &&
                strcmp(AST_CAR(AST_CAR(candidate))->as.symbol,
                       function->name) == 0) {
                definition = candidate;
                break;
            }
        }
        if (!definition) {
            type_error(errbuf, errbuf_size,
                       "type error: definition for %s disappeared",
                       function->name);
            return 0;
        }
        AstNode *params = AST_CDR(AST_CAR(definition));
        for (int p = 0; p < function->arity; p++) {
            AstNode *name = AST_CAR(params);
            type_env_bind(&locals, name->as.symbol, function->params[p]);
            params = AST_CDR(params);
        }
        Type *actual = infer_body(function->body, &locals, program, &ok,
                                  errbuf, errbuf_size);
        if (ok && function->result_annotated &&
            !compatible(actual, function->result)) {
            type_error(errbuf, errbuf_size,
                       "type error in %s: expected return %s, got %s",
                       function->name, type_name(function->result),
                       type_name(actual));
            ok = 0;
        }
    }
    return ok;
}
