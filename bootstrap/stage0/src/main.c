/*
 * main.c — NPL stage0 compiler driver.
 *
 * Usage:
 *   nplc0 --print-ast <file.npl>     Read and print AST (S-expr)
 *   nplc0 -o <output> <file.npl>     Compile to executable
 *   nplc0 --emit-qbe <file.npl>      Emit QBE IL to stdout
 *   nplc0 --help                     Show usage
 *   nplc0 <file.npl>                 Read and print AST (default)
 *
 * Phase 2: Added QBE IL generation and compilation pipeline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "reader.h"
#include "symtab.h"
#include "qbe_gen.h"
#include "desugar.h"

typedef struct IncludeStack IncludeStack;
struct IncludeStack {
    const char *path;
    const IncludeStack *parent;
};

static char *copy_string(const char *text)
{
    size_t size = strlen(text) + 1;
    char *copy = malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static char *resolve_include_path(const char *source_path, const char *included)
{
    if (included[0] == '/') return copy_string(included);

    const char *slash = strrchr(source_path, '/');
    size_t directory_size = slash ? (size_t)(slash - source_path + 1) : 0;
    size_t included_size = strlen(included);
    char *path = malloc(directory_size + included_size + 1);
    if (!path) return NULL;
    if (directory_size) memcpy(path, source_path, directory_size);
    memcpy(path + directory_size, included, included_size + 1);
    return path;
}

static int include_stack_contains(const IncludeStack *stack, const char *path)
{
    while (stack) {
        if (strcmp(stack->path, path) == 0) return 1;
        stack = stack->parent;
    }
    return 0;
}

static int include_form(const AstNode *node)
{
    return AST_IS_PAIR(node) && AST_CAR(node)->tag == AST_SYMBOL &&
           strcmp(AST_CAR(node)->as.symbol, "include") == 0;
}

static void append_forms(AstNode **head, AstNode **tail, AstNode *forms)
{
    while (AST_IS_PAIR(forms)) {
        AstNode *cell = ast_pair(AST_CAR(forms), ast_nil());
        if (*tail) AST_CDR(*tail) = cell;
        else *head = cell;
        *tail = cell;
        forms = AST_CDR(forms);
    }
}

static AstNode *load_program(const char *path, const IncludeStack *parent,
                             char *error, size_t error_size)
{
    if (include_stack_contains(parent, path)) {
        snprintf(error, error_size, "include cycle: %s", path);
        return NULL;
    }

    char *source = reader_read_file(path);
    if (!source) {
        snprintf(error, error_size, "cannot read '%s'", path);
        return NULL;
    }
    AstNode *parsed = reader_read_all(source, error, error_size);
    free(source);
    if (!parsed) return NULL;

    IncludeStack stack = { path, parent };
    AstNode *result = ast_nil();
    AstNode *tail = NULL;
    while (AST_IS_PAIR(parsed)) {
        AstNode *form = AST_CAR(parsed);
        parsed = AST_CDR(parsed);
        if (!include_form(form)) {
            AstNode *cell = ast_pair(form, ast_nil());
            if (tail) AST_CDR(tail) = cell;
            else result = cell;
            tail = cell;
            continue;
        }

        AstNode *paths = AST_CDR(form);
        if (!AST_IS_PAIR(paths)) {
            snprintf(error, error_size,
                     "include in '%s' needs at least one path", path);
            return NULL;
        }
        while (AST_IS_PAIR(paths)) {
            AstNode *item = AST_CAR(paths);
            if (item->tag != AST_STRING) {
                snprintf(error, error_size,
                         "include path in '%s' must be a string", path);
                return NULL;
            }
            char *resolved = resolve_include_path(path, item->as.string);
            if (!resolved) {
                snprintf(error, error_size, "out of memory");
                return NULL;
            }
            AstNode *included = load_program(resolved, &stack,
                                             error, error_size);
            free(resolved);
            if (!included) return NULL;
            append_forms(&result, &tail, included);
            paths = AST_CDR(paths);
        }
        if (!AST_IS_NIL(paths)) {
            snprintf(error, error_size,
                     "include paths in '%s' must form a proper list", path);
            return NULL;
        }
    }
    return result;
}

/* Paths to external tools — configured at build time */
#ifndef QBE_PATH
#define QBE_PATH "qbe"
#endif

#ifndef AS_BIN
#define AS_BIN "/usr/bin/as"
#endif

#ifndef CC_BIN
#define CC_BIN "/usr/bin/cc"
#endif

#ifndef RUNTIME_LIB_PATH
#define RUNTIME_LIB_PATH ""
#endif

static void usage(const char *prog)
{
    fprintf(stderr,
        "NPL stage0 compiler (nplc0)\n"
        "Usage:\n"
        "  %s --print-ast <file.npl>     Read and print AST\n"
        "  %s --emit-qbe <file.npl>      Emit QBE IL to stdout\n"
        "  %s -o <output> <file.npl>     Compile to executable\n"
        "  %s --help                     Show this help\n"
        "  %s <file.npl>                 Read and print AST (default)\n",
        prog, prog, prog, prog, prog);
}

/* Compile NPL source to executable */
static int compile(const char *filename, const char *output)
{
    char errbuf[512];
    AstNode *ast = load_program(filename, NULL, errbuf, sizeof(errbuf));
    if (!ast) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return 1;
    }

    ast = desugar_program(ast);

    /* Generate QBE IL */
    char ssa_path[1024];
    snprintf(ssa_path, sizeof(ssa_path), "%s.ssa", output);

    FILE *ssa_file = fopen(ssa_path, "w");
    if (!ssa_file) {
        fprintf(stderr, "Error: cannot create '%s'\n", ssa_path);
        return 1;
    }

    SymTab symtab;
    symtab_init(&symtab);

    QbeCtx ctx;
    qbe_ctx_init(&ctx, ssa_file, &symtab);
    qbe_gen_program(&ctx, ast);
    fclose(ssa_file);

    /* Run QBE to generate assembly */
    char s_path[1024];
    snprintf(s_path, sizeof(s_path), "%s.s", output);

    char qbe_cmd[2048];
    snprintf(qbe_cmd, sizeof(qbe_cmd), "%s -o %s %s",
             QBE_PATH, s_path, ssa_path);

    int rc = system(qbe_cmd);
    if (rc != 0) {
        fprintf(stderr, "Error: QBE failed (exit %d)\n", rc);
        return 1;
    }

    /* Assemble and link with runtime */
    char link_cmd[2048];
    if (strlen(RUNTIME_LIB_PATH) > 0) {
        snprintf(link_cmd, sizeof(link_cmd), "%s -o %s %s %s",
                 CC_BIN, output, s_path, RUNTIME_LIB_PATH);
    } else {
        snprintf(link_cmd, sizeof(link_cmd), "%s -o %s %s",
                 CC_BIN, output, s_path);
    }

    rc = system(link_cmd);
    if (rc != 0) {
        fprintf(stderr, "Error: link failed (exit %d)\n", rc);
        return 1;
    }

    /* Cleanup intermediate files */
    remove(ssa_path);
    remove(s_path);

    return 0;
}

/* Emit QBE IL to stdout */
static int emit_qbe(const char *filename)
{
    char errbuf[512];
    AstNode *ast = load_program(filename, NULL, errbuf, sizeof(errbuf));
    if (!ast) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return 1;
    }

    ast = desugar_program(ast);

    SymTab symtab;
    symtab_init(&symtab);

    QbeCtx ctx;
    qbe_ctx_init(&ctx, stdout, &symtab);
    qbe_gen_program(&ctx, ast);

    return 0;
}

int main(int argc, char **argv)
{
    const char *filename = NULL;
    const char *output = NULL;
    int do_emit_qbe = 0;
    const char *prog = argv[0];

    /* Parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(prog);
            return 0;
        } else if (strcmp(argv[i], "--print-ast") == 0) {
            /* Print AST mode — handled below */
        } else if (strcmp(argv[i], "--emit-qbe") == 0) {
            do_emit_qbe = 1;
        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) {
                output = argv[++i];
            } else {
                fprintf(stderr, "Error: -o requires an argument\n");
                return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(prog);
            return 1;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Error: no input file\n");
        usage(prog);
        return 1;
    }

    /* Compile mode */
    if (output) {
        return compile(filename, output);
    }

    /* Emit QBE IL mode */
    if (do_emit_qbe) {
        return emit_qbe(filename);
    }

    /* Print AST mode (default) */
    char errbuf[512];
    AstNode *ast = load_program(filename, NULL, errbuf, sizeof(errbuf));
    if (!ast) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return 1;
    }

    ast_print(ast);
    printf("\n");

    ast_free_all();
    return 0;
}
