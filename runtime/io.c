#define _DARWIN_C_SOURCE

/* Process context and host services: program arguments, whole-file input,
 * child processes, environment, files and directories, executable path,
 * exit. See port.h for the module split. */

#include "port.h"
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char **environ;

static int g_argc_stored = 0;
static char **g_argv_stored = NULL;
static NplVal g_args_list = 2;
static NplVal g_command_line = 2;

void npl_port_init_internal(void);

void npl_io_init(void)
{
    g_args_list = npl_nil();
    g_command_line = npl_nil();
    npl_gc_root(&g_args_list);
    npl_gc_root(&g_command_line);
    npl_port_init_internal();
}


void npl_store_args(int argc, char **argv) {
#ifdef NPL_UNBUFFERED_STDOUT
    setvbuf(stdout, (char *)NULL, _IONBF, 0);
#endif
    g_argc_stored = argc;
    g_argv_stored = argv;
}

static NplVal argument_list(int first)
{
    NplVal acc = npl_nil();
    npl_gc_root(&acc);
    for (int i = g_argc_stored - 1; i >= first; i--) {
        NplVal s = npl_c_text_internal(g_argv_stored[i], strlen(g_argv_stored[i]));
        acc = npl_cons(s, acc);
    }
    npl_gc_unroot(&acc);
    return acc;
}

/* Arguments after the program name (argv[1..]); compiler/driver use. */
NplVal npl_program_arguments(void) {
    if (!npl_is_nil(g_args_list)) return g_args_list;
    g_args_list = argument_list(1);
    return g_args_list;
}

/* R7RS command-line: the command name followed by its arguments. A runner
 * such as npli replaces it with the script name and script arguments. */
NplVal npl_command_line(void) {
    if (npl_is_nil(g_command_line)) g_command_line = argument_list(0);
    return g_command_line;
}

NplVal npl_set_command_line(NplVal arguments) {
    g_command_line = arguments;
    return npl_nil();
}

NplVal npl_read_file(NplVal path) {
    char *buf = npl_c_string_internal(npl_require_bytes_internal(path, "read-file"));
    FILE *fp = fopen(buf, "rb");
    free(buf);
    if (!fp) npl_primitive_file_error("read-file", "cannot open file", path);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) {
        fclose(fp);
        npl_primitive_file_error("read-file", "cannot read file", path);
    }
    char *data = (char *)malloc((size_t)size + 1);
    size_t got = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    data[got] = '\0';
    NplVal out = npl_c_text_internal(data, got);
    free(data);
    return out;
}

static const NplString *process_string(NplVal value, const char *who)
{
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_STRING)
        npl_primitive_error(who, "expected a string", value);
    return (const NplString *)npl_header(value);
}

static size_t process_argument_count(NplVal arguments)
{
    size_t count = 0;
    while (!npl_is_nil(arguments)) {
        if (!npl_is_ptr(arguments) || npl_header(arguments)->tag != BOX_PAIR)
            npl_primitive_error("run-process", "expected a proper list",
                                arguments);
        (void)process_string(npl_car(arguments), "run-process");
        arguments = npl_cdr(arguments);
        count++;
    }
    if (count == 0)
        npl_primitive_error("run-process", "expected a program name",
                            npl_nil());
    return count;
}

NplVal npl_process_run(NplVal arguments, NplVal output_path)
{
    size_t count = process_argument_count(arguments);
    char **argv = (char **)malloc((count + 1) * sizeof(char *));
    if (!argv) npl_runtime_oom_internal();
    NplVal rest = arguments;
    for (size_t index = 0; index < count; index++) {
        argv[index] = npl_c_string_internal(process_string(npl_car(rest), "run-process"));
        rest = npl_cdr(rest);
    }
    argv[count] = NULL;
    char *output_file = NULL;

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_t *actions_pointer = NULL;
    int action_status = 0;
    if (output_path != npl_false()) {
        output_file = npl_c_string_internal(process_string(output_path, "run-process"));
        action_status = posix_spawn_file_actions_init(&actions);
        if (action_status == 0) {
            action_status = posix_spawn_file_actions_addopen(
                &actions, STDOUT_FILENO, output_file,
                O_WRONLY | O_CREAT | O_TRUNC, 0666);
            actions_pointer = &actions;
        }
    }
    if (action_status != 0) {
        for (size_t index = 0; index < count; index++) free(argv[index]);
        free(argv);
        free(output_file);
        npl_primitive_file_error("run-process", "cannot configure output",
                                 output_path);
    }

    pid_t child = 0;
    int spawn_status = posix_spawnp(
        &child, argv[0], actions_pointer, NULL, argv, environ);
    if (actions_pointer) posix_spawn_file_actions_destroy(actions_pointer);
    for (size_t index = 0; index < count; index++) free(argv[index]);
    free(argv);
    free(output_file);
    if (spawn_status != 0) return npl_fixnum(127);

    int status = 0;
    if (waitpid(child, &status, 0) < 0)
        npl_primitive_error("run-process", "wait failed", arguments);
    if (WIFEXITED(status)) return npl_fixnum(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return npl_fixnum(128 + WTERMSIG(status));
    return npl_fixnum(1);
}

NplVal npl_get_environment_variable(NplVal name_value)
{
    char *name = npl_c_string_internal(process_string(name_value,
                                         "get-environment-variable"));
    const char *value = getenv(name);
    free(name);
    return value ? npl_c_text_internal(value, strlen(value)) : npl_false();
}

NplVal npl_get_environment_variables(void)
{
    NplVal variables = npl_nil();
    NplVal name = npl_nil();
    NplVal value = npl_nil();
    npl_gc_root(&variables);
    npl_gc_root(&name);
    npl_gc_root(&value);
    for (size_t index = 0; environ[index]; index++) {
        const char *entry = environ[index];
        const char *separator = strchr(entry, '=');
        if (!separator) continue;
        name = npl_c_text_internal(entry, (size_t)(separator - entry));
        value = npl_c_text_internal(separator + 1, strlen(separator + 1));
        value = npl_cons(name, value);
        variables = npl_cons(value, variables);
    }
    npl_gc_unroot(&value);
    npl_gc_unroot(&name);
    npl_gc_unroot(&variables);
    return variables;
}

NplVal npl_file_exists(NplVal path_value)
{
    char *path = npl_c_string_internal(process_string(path_value, "file-exists?"));
    int exists = access(path, F_OK) == 0;
    free(path);
    return exists ? npl_true() : npl_false();
}

NplVal npl_delete_file(NplVal path_value)
{
    char *path = npl_c_string_internal(process_string(path_value, "delete-file"));
    int failed = unlink(path) != 0;
    free(path);
    if (failed)
        npl_primitive_file_error("delete-file", "cannot delete file",
                                 path_value);
    return npl_nil();
}

NplVal npl_make_temporary_directory(void)
{
    const char *root = getenv("TMPDIR");
    if (!root || root[0] == '\0') root = "/tmp";
    size_t length = strlen(root) + sizeof("/npl-driver.XXXXXX");
    char *path = (char *)malloc(length);
    if (!path) npl_runtime_oom_internal();
    (void)snprintf(path, length, "%s/npl-driver.XXXXXX", root);
    if (!mkdtemp(path)) {
        free(path);
        npl_primitive_file_error("make-temporary-directory",
                                 "cannot create directory",
                                 npl_c_text_internal(root, strlen(root)));
    }
    NplVal value = npl_c_text_internal(path, strlen(path));
    free(path);
    return value;
}

NplVal npl_delete_directory(NplVal path_value)
{
    char *path = npl_c_string_internal(process_string(path_value, "delete-directory"));
    int failed = rmdir(path) != 0;
    free(path);
    if (failed)
        npl_primitive_file_error("delete-directory", "cannot delete directory",
                                 path_value);
    return npl_nil();
}

/* Absolute path of the running executable, or #f when the OS cannot say.
 * Tool and library lookup policy built on it lives in NPL. */
NplVal npl_executable_path(void)
{
    char raw[PATH_MAX];
    char resolved[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = (uint32_t)sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) return npl_false();
#else
    ssize_t length = readlink("/proc/self/exe", raw, sizeof(raw) - 1);
    if (length < 0) return npl_false();
    raw[length] = '\0';
#endif
    if (!realpath(raw, resolved)) return npl_false();
    return npl_c_text_internal(resolved, strlen(resolved));
}

void npl_exit(NplVal code) {
    int64_t v = (int64_t)(code >> 2);          /* untag fixnum */
    fflush(stdout);
    _Exit((int)(v & 255));
}
