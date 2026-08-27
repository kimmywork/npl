#ifndef NPL_TYPE_H
#define NPL_TYPE_H

#include "ast.h"
#include <stdint.h>

/*
 * NPL Type System — Phase 5
 *
 * Simple type system with annotations.
 * Supports: Integer, Float, Bool, String, (-> A B), Dynamic, type variables.
 *
 * This is NOT a full HM implementation — it's a practical subset
 * that allows typed code to be compiled as unboxed values.
 */

/* ---- Type representation ---- */

typedef enum {
    TYPE_DYNAMIC,   /* Untyped — runtime tagged value */
    TYPE_INTEGER,   /* Machine-width integer (unboxed: w in QBE) */
    TYPE_FLOAT,     /* Double-precision float (unboxed: d in QBE) */
    TYPE_BOOL,      /* Boolean (unboxed: w, 0 or 1) */
    TYPE_STRING,    /* String (boxed pointer) */
    TYPE_VOID,      /* No return value */
    TYPE_VAR,       /* Type variable (for inference) */
    TYPE_FUNC,      /* Function type: (-> A B) */
    TYPE_PAIR,      /* Pair type: (Pair A B) */
} TypeKind;

typedef struct Type Type;

struct Type {
    TypeKind kind;
    union {
        /* TYPE_VAR */
        struct {
            int id;           /* Unique type variable id */
            Type *instance;   /* For unification (union-find) */
        } var;
        /* TYPE_FUNC */
        struct {
            Type *param;    /* Parameter type */
            Type *result;   /* Return type */
        } func;
        /* TYPE_PAIR */
        struct {
            Type *car;
            Type *cdr;
        } pair;
    } as;
};

/* ---- Type constructors ---- */

Type *type_dynamic(void);
Type *type_integer(void);
Type *type_float(void);
Type *type_bool(void);
Type *type_string(void);
Type *type_void(void);
Type *type_var(int id);
Type *type_func(Type *param, Type *result);
Type *type_pair(Type *car, Type *cdr);

/* ---- Type comparison ---- */

int type_equal(Type *a, Type *b);
int type_is_numeric(Type *t);

/* ---- Type environment ---- */

#define TYPE_ENV_MAX 512

typedef struct {
    const char *names[TYPE_ENV_MAX];
    Type       *types[TYPE_ENV_MAX];
    int         count;
} TypeEnv;

void type_env_init(TypeEnv *env);
void type_env_bind(TypeEnv *env, const char *name, Type *type);
Type *type_env_lookup(TypeEnv *env, const char *name);

/* ---- Annotation parsing ---- */

/* Parse a type annotation from an AST node.
 * Examples:
 *   Integer -> TYPE_INTEGER
 *   Float   -> TYPE_FLOAT
 *   Bool    -> TYPE_BOOL
 *   (-> Integer Integer) -> TYPE_FUNC(TYPE_INTEGER, TYPE_INTEGER)
 *   Dynamic -> TYPE_DYNAMIC
 */
Type *parse_type_annotation(AstNode *node);

/* ---- Type checking ---- */

/* Check that an expression has the expected type.
 * Returns 1 on success, 0 on type error (error message in errbuf).
 * May update the type environment with inferred types.
 */
int type_check(AstNode *expr, Type *expected, TypeEnv *env, char *errbuf, size_t errbuf_size);

/* Infer the type of an expression.
 * Returns the inferred type, or NULL on error.
 */
Type *type_infer(AstNode *expr, TypeEnv *env, char *errbuf, size_t errbuf_size);

/* ---- QBE type mapping ---- */

/* Get the QBE type string for a given NPL type.
 * Returns "w" for integer/bool, "d" for float, "l" for pointers/dynamic.
 */
const char *qbe_type_for(Type *type);

/* ---- Guard generation ---- */

/* Generate a guard check for typed/untyped boundary.
 * Returns 1 if guard was generated, 0 if no guard needed.
 */
int type_needs_guard(Type *from, Type *to);

/* ---- Program annotations ---- */

#define TYPE_PROGRAM_MAX 256
#define TYPE_MAX_PARAMS 16

typedef struct {
    const char *name;
    int arity;
    Type *params[TYPE_MAX_PARAMS];
    unsigned char param_annotated[TYPE_MAX_PARAMS];
    Type *result;
    int result_annotated;
    AstNode *body;
} TypedFunction;

typedef struct {
    const char *name;
    Type *type;
    int annotated;
    AstNode *value;
} TypedVariable;

typedef struct {
    TypedFunction functions[TYPE_PROGRAM_MAX];
    int function_count;
    TypedVariable variables[TYPE_PROGRAM_MAX];
    int variable_count;
} TypeProgram;

void type_program_init(TypeProgram *program);
int type_analyze_program(AstNode *ast, TypeProgram *program,
                         char *errbuf, size_t errbuf_size);
TypedFunction *type_program_function(TypeProgram *program, const char *name);
TypedVariable *type_program_variable(TypeProgram *program, const char *name);
const char *type_name(Type *type);

#endif /* NPL_TYPE_H */
