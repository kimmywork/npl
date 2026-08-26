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
#include "macro.h"
#include "desugar.h"

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
    /* Read file */
    char *source = reader_read_file(filename);
    if (!source) {
        fprintf(stderr, "Error: cannot read '%s'\n", filename);
        return 1;
    }

    /* Parse */
    char errbuf[512];
    AstNode *ast = reader_read_all(source, errbuf, sizeof(errbuf));
    free(source);

    if (!ast) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return 1;
    }

    /* Expand macros */
    MacroEnv macro_env;
    macro_env_init(&macro_env);
    ast = macro_expand_program(&macro_env, ast);

    /* Desugar core forms (cond -> if chains etc.) */
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
    char *source = reader_read_file(filename);
    if (!source) {
        fprintf(stderr, "Error: cannot read '%s'\n", filename);
        return 1;
    }

    char errbuf[512];
    AstNode *ast = reader_read_all(source, errbuf, sizeof(errbuf));
    free(source);

    if (!ast) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return 1;
    }

    /* Expand macros */
    MacroEnv macro_env;
    macro_env_init(&macro_env);
    ast = macro_expand_program(&macro_env, ast);

    /* Desugar core forms (cond -> if chains etc.) */
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
    char *source = reader_read_file(filename);
    if (!source) {
        fprintf(stderr, "Error: cannot read '%s'\n", filename);
        return 1;
    }

    char errbuf[512];
    AstNode *ast = reader_read_all(source, errbuf, sizeof(errbuf));
    free(source);

    if (!ast) {
        fprintf(stderr, "Error: %s\n", errbuf);
        return 1;
    }

    ast_print(ast);
    printf("\n");

    ast_free_all();
    return 0;
}
