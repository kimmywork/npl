#include "ast.h"
#include "reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static char *resolve_path(const char *source_path, const char *included)
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

static int stack_contains(const IncludeStack *stack, const char *path)
{
    while (stack) {
        if (strcmp(stack->path, path) == 0) return 1;
        stack = stack->parent;
    }
    return 0;
}

static int symbol_named(const AstNode *node, const char *name)
{
    return node && node->tag == AST_SYMBOL &&
           strcmp(node->as.symbol, name) == 0;
}

static int include_form(const AstNode *node)
{
    return ast_is_pair(node) && symbol_named(AST_CAR(node), "include");
}

static void append_forms(AstNode **head, AstNode **tail, AstNode *forms)
{
    while (ast_is_pair(forms)) {
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
    if (stack_contains(parent, path)) {
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

    while (ast_is_pair(parsed)) {
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
        if (!ast_is_pair(paths)) {
            snprintf(error, error_size,
                     "include in '%s' needs at least one path", path);
            return NULL;
        }
        while (ast_is_pair(paths)) {
            AstNode *item = AST_CAR(paths);
            if (item->tag != AST_STRING) {
                snprintf(error, error_size,
                         "include path in '%s' must be a string", path);
                return NULL;
            }
            char *resolved = resolve_path(path, item->as.string);
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
        if (!ast_is_nil(paths)) {
            snprintf(error, error_size,
                     "include paths in '%s' must form a proper list", path);
            return NULL;
        }
    }
    return result;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s [--print-ast] file.npl\n", program);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--print-ast") == 0) continue;
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (argv[i][0] == '-' || path) {
            usage(argv[0]);
            return 2;
        }
        path = argv[i];
    }
    if (!path) {
        usage(argv[0]);
        return 2;
    }

    char error[512] = {0};
    AstNode *program = load_program(path, NULL, error, sizeof(error));
    if (!program) {
        fprintf(stderr, "error: %s\n", error);
        ast_free_all();
        return 1;
    }

    ast_print(program);
    putchar('\n');
    ast_free_all();
    return 0;
}
