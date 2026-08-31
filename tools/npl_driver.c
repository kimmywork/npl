#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#ifndef NPL_DEFAULT_RAW_COMPILER
#define NPL_DEFAULT_RAW_COMPILER "build/bin/nplc-raw"
#endif
#ifndef NPL_DEFAULT_QBE
#define NPL_DEFAULT_QBE "build/qbe/qbe"
#endif
#ifndef NPL_DEFAULT_RUNTIME
#define NPL_DEFAULT_RUNTIME "build/runtime/libnpl_runtime.a"
#endif
#ifndef NPL_DEFAULT_STDLIB
#define NPL_DEFAULT_STDLIB "lib"
#endif
#ifndef NPL_DEFAULT_CC
#define NPL_DEFAULT_CC "cc"
#endif

static void usage(FILE *stream)
{
    fprintf(stream,
        "usage: nplc [options] <source.npl> -o <output>\n"
        "       nplc --emit-qbe [options] <source.npl>\n"
        "options:\n"
        "  --stdlib-root DIR\n"
        "  --raw-compiler PATH\n"
        "  --qbe PATH\n"
        "  --runtime PATH\n"
        "  --cc PATH\n");
}

static int child_status(int status)
{
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}

static int run_child(char *const argv[], const char *stdout_path)
{
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_t *actions_pointer = NULL;
    if (stdout_path) {
        if (posix_spawn_file_actions_init(&actions) != 0 ||
            posix_spawn_file_actions_addopen(
                &actions, STDOUT_FILENO, stdout_path,
                O_WRONLY | O_CREAT | O_TRUNC, 0666) != 0) {
            fprintf(stderr, "nplc: cannot configure child output\n");
            return 1;
        }
        actions_pointer = &actions;
    }

    pid_t child = 0;
    int spawn_status = posix_spawnp(
        &child, argv[0], actions_pointer, NULL, argv, environ);
    if (actions_pointer) posix_spawn_file_actions_destroy(actions_pointer);
    if (spawn_status != 0) {
        fprintf(stderr, "nplc: cannot execute %s\n", argv[0]);
        return 127;
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        fprintf(stderr, "nplc: cannot wait for %s\n", argv[0]);
        return 1;
    }
    return child_status(status);
}

static void relay_file(FILE *output, const char *path)
{
    FILE *input = fopen(path, "rb");
    if (!input) return;
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0)
        (void)fwrite(buffer, 1, count, output);
    fclose(input);
}

static int temp_path(char *output, size_t capacity,
                     const char *directory, const char *name)
{
    int length = snprintf(output, capacity, "%s/%s", directory, name);
    return length >= 0 && (size_t)length < capacity;
}

int main(int argc, char **argv)
{
    const char *raw_compiler = NPL_DEFAULT_RAW_COMPILER;
    const char *qbe = NPL_DEFAULT_QBE;
    const char *runtime = NPL_DEFAULT_RUNTIME;
    const char *stdlib = NPL_DEFAULT_STDLIB;
    const char *cc = NPL_DEFAULT_CC;
    const char *source = NULL;
    const char *output = NULL;
    int emit_qbe = 0;

    for (int index = 1; index < argc; index++) {
        const char *argument = argv[index];
        if (strcmp(argument, "--emit-qbe") == 0) {
            emit_qbe = 1;
        } else if (strcmp(argument, "-o") == 0 ||
                   strcmp(argument, "--stdlib-root") == 0 ||
                   strcmp(argument, "--raw-compiler") == 0 ||
                   strcmp(argument, "--qbe") == 0 ||
                   strcmp(argument, "--runtime") == 0 ||
                   strcmp(argument, "--cc") == 0) {
            if (++index >= argc) {
                fprintf(stderr, "nplc: %s needs a value\n", argument);
                return 2;
            }
            const char *value = argv[index];
            if (strcmp(argument, "-o") == 0) output = value;
            else if (strcmp(argument, "--stdlib-root") == 0) stdlib = value;
            else if (strcmp(argument, "--raw-compiler") == 0)
                raw_compiler = value;
            else if (strcmp(argument, "--qbe") == 0) qbe = value;
            else if (strcmp(argument, "--runtime") == 0) runtime = value;
            else cc = value;
        } else if (argument[0] == '-') {
            fprintf(stderr, "nplc: unknown option %s\n", argument);
            usage(stderr);
            return 2;
        } else if (!source) {
            source = argument;
        } else {
            fprintf(stderr, "nplc: multiple source files are not supported\n");
            return 2;
        }
    }

    if (!source || (!emit_qbe && !output) || (emit_qbe && output)) {
        usage(stderr);
        return 2;
    }

    char *compiler_argv[] = {
        (char *)raw_compiler,
        "--emit-qbe",
        "--stdlib-root",
        (char *)stdlib,
        (char *)source,
        NULL
    };
    if (emit_qbe) return run_child(compiler_argv, NULL);

    const char *temporary_root = getenv("TMPDIR");
    if (!temporary_root || temporary_root[0] == '\0') temporary_root = "/tmp";
    char directory[4096];
    int directory_length = snprintf(
        directory, sizeof(directory), "%s/nplc.XXXXXX", temporary_root);
    if (directory_length < 0 || (size_t)directory_length >= sizeof(directory) ||
        !mkdtemp(directory)) {
        fprintf(stderr, "nplc: cannot create temporary directory\n");
        return 1;
    }
    char ssa_path[4096];
    char asm_path[4096];
    if (!temp_path(ssa_path, sizeof(ssa_path), directory, "source.ssa") ||
        !temp_path(asm_path, sizeof(asm_path), directory, "source.s")) {
        fprintf(stderr, "nplc: temporary path is too long\n");
        rmdir(directory);
        return 1;
    }

    int status = run_child(compiler_argv, ssa_path);
    if (status != 44) {
        relay_file(stderr, ssa_path);
        unlink(ssa_path);
        rmdir(directory);
        return status == 0 ? 1 : status;
    }

    char *qbe_argv[] = {
        (char *)qbe, "-o", asm_path, ssa_path, NULL
    };
    status = run_child(qbe_argv, NULL);
    if (status == 0) {
        char *cc_argv[] = {
            (char *)cc, "-o", (char *)output, asm_path,
            (char *)runtime, NULL
        };
        status = run_child(cc_argv, NULL);
    }

    unlink(asm_path);
    unlink(ssa_path);
    rmdir(directory);
    return status;
}
