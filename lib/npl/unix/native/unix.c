/* (npl unix) native entry points: the POSIX file/process/database surface
 * of SRFI 170 that does not need a Scheme port.  Core package (mode A);
 * NPL wrappers turn the returned vectors into records and time values.
 */

#define _DARWIN_C_SOURCE
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "npl.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#define STAT_ATIM(s) ((s).st_atimespec)
#define STAT_MTIM(s) ((s).st_mtimespec)
#define STAT_CTIM(s) ((s).st_ctimespec)
#else
#define STAT_ATIM(s) ((s).st_atim)
#define STAT_MTIM(s) ((s).st_mtim)
#define STAT_CTIM(s) ((s).st_ctim)
#endif

static char *unix_path(NplVal value, const char *who)
{
    return npl_c_string_internal(npl_require_bytes_internal(value, who));
}

static const char *errno_name(int code)
{
    switch (code) {
#define CASE(name) case name: return #name
        CASE(EACCES); CASE(EEXIST); CASE(EINVAL); CASE(EISDIR);
        CASE(ENOENT); CASE(ENOTDIR); CASE(ENOTEMPTY); CASE(EPERM);
        CASE(ENOSPC); CASE(EROFS); CASE(EMFILE); CASE(ENFILE);
        CASE(ELOOP); CASE(ENAMETOOLONG); CASE(ENOTSUP); CASE(EBADF);
        CASE(EAGAIN); CASE(EINTR); CASE(ENOMEM); CASE(EXDEV);
        CASE(ESRCH); CASE(ECHILD); CASE(ENOSYS);
#undef CASE
        default: return "EUNKNOWN";
    }
}

/* Raise an error object whose kind is "posix"; irritants carry the symbolic
 * errno name first, then the caller's value. */
static void unix_raise(const char *who, int code, NplVal irritant)
{
    NplVal fields[4];
    NplVal name = npl_symbol(errno_name(code),
                             (uint32_t)strlen(errno_name(code)));
    NplVal message = npl_string(strerror(code),
                                (uint32_t)strlen(strerror(code)));
    NplVal irritants = npl_cons(name, npl_cons(irritant, npl_nil()));
    fields[0] = npl_symbol("npl-error-object", 16);
    fields[1] = npl_symbol("posix", 5);
    fields[2] = message;
    fields[3] = irritants;
    {
        NplVal object = npl_make_record(4, fields);
        npl_raise_record(object);
    }
}

static int unix_check_string_equals(NplVal value, const char *text,
                                    uint32_t length)
{
    const NplString *string;
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_SYMBOL)
        return 0;
    string = (const NplString *)npl_header(value);
    return string->length == length &&
           memcmp(string->data, text, length) == 0;
}

/* glob(3): a POSIX pattern expanded by the host.  No matches is the empty
 * list, not an error. */
NplVal npl_unix_glob(NplVal pattern_value)
{
    char *pattern = unix_path(pattern_value, "glob");
    glob_t matches;
    int status = glob(pattern, 0, NULL, &matches);
    free(pattern);
    if (status == GLOB_NOMATCH) return npl_nil();
    if (status != 0) unix_raise("glob", errno, pattern_value);
    {
        NplVal result = npl_nil();
        npl_gc_root(&result);
        for (size_t index = matches.gl_pathc; index > 0; index--)
            result = npl_cons(
                npl_string(matches.gl_pathv[index - 1],
                           (uint32_t)strlen(matches.gl_pathv[index - 1])),
                result);
        globfree(&matches);
        npl_gc_unroot(&result);
        return result;
    }
}

NplVal npl_unix_posix_error_p(NplVal value)
{
    NplVector *record;
    if (!npl_is_ptr(value) || npl_header(value)->tag != BOX_RECORD)
        return npl_false();
    record = (NplVector *)npl_header(value);
    if (record->length != 4) return npl_false();
    return unix_check_string_equals(record->data[0], "npl-error-object", 16) &&
           unix_check_string_equals(record->data[1], "posix", 5)
               ? npl_true() : npl_false();
}

NplVal npl_unix_posix_error_name(NplVal value)
{
    NplVector *record = (NplVector *)npl_header(value);
    if (!npl_unix_posix_error_p(value)) return npl_false();
    if (npl_is_nil(record->data[3]) || !npl_is_ptr(record->data[3]))
        return npl_false();
    return npl_car(record->data[3]);
}

NplVal npl_unix_posix_error_message(NplVal value)
{
    NplVector *record = (NplVector *)npl_header(value);
    if (!npl_unix_posix_error_p(value)) return npl_false();
    return record->data[2];
}

static NplVal unix_stat_fields(const struct stat *info)
{
    NplVal fields[16];
    fields[0] = npl_fixnum((int64_t)info->st_dev);
    fields[1] = npl_fixnum((int64_t)info->st_ino);
    fields[2] = npl_fixnum((int64_t)info->st_mode);
    fields[3] = npl_fixnum((int64_t)info->st_nlink);
    fields[4] = npl_fixnum((int64_t)info->st_uid);
    fields[5] = npl_fixnum((int64_t)info->st_gid);
    fields[6] = npl_fixnum((int64_t)info->st_rdev);
    fields[7] = npl_fixnum((int64_t)info->st_size);
    fields[8] = npl_fixnum((int64_t)info->st_blksize);
    fields[9] = npl_fixnum((int64_t)info->st_blocks);
    fields[10] = npl_fixnum((int64_t)STAT_ATIM(*info).tv_sec);
    fields[11] = npl_fixnum((int64_t)STAT_ATIM(*info).tv_nsec);
    fields[12] = npl_fixnum((int64_t)STAT_MTIM(*info).tv_sec);
    fields[13] = npl_fixnum((int64_t)STAT_MTIM(*info).tv_nsec);
    fields[14] = npl_fixnum((int64_t)STAT_CTIM(*info).tv_sec);
    fields[15] = npl_fixnum((int64_t)STAT_CTIM(*info).tv_nsec);
    return npl_make_vector(16, fields);
}

NplVal npl_unix_file_info(NplVal path_value, NplVal follow_value)
{
    char *path = unix_path(path_value, "file-info");
    struct stat info;
    int status = npl_is_true(follow_value) ? stat(path, &info)
                                           : lstat(path, &info);
    int code = errno;
    free(path);
    if (status != 0) unix_raise("file-info", code, path_value);
    return unix_stat_fields(&info);
}

NplVal npl_unix_file_info_port(NplVal port)
{
    NplVal fd = npl_port_fileno(port);
    struct stat info;
    if (npl_is_false(fd))
        npl_primitive_error("file-info", "expected a file port", port);
    if (fstat((int)npl_fixnum_value(fd), &info) != 0)
        unix_raise("file-info", errno, port);
    return unix_stat_fields(&info);
}

static int unix_dotfile(const char *name)
{
    return name[0] == '.' && name[1] != '\0';
}

NplVal npl_unix_directory_files(NplVal path_value, NplVal dotfiles_value)
{
    char *path = unix_path(path_value, "directory-files");
    DIR *directory = opendir(path);
    NplVal result = npl_nil();
    NplVal head;
    struct dirent *entry;
    int include_dotfiles = npl_is_true(dotfiles_value);
    if (!directory) {
        int code = errno;
        free(path);
        unix_raise("directory-files", code, path_value);
    }
    npl_gc_root(&result);
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (!include_dotfiles && unix_dotfile(entry->d_name))
            continue;
        result = npl_cons(npl_string(entry->d_name,
                                     (uint32_t)strlen(entry->d_name)),
                          result);
    }
    closedir(directory);
    free(path);
    /* readdir returns reverse order; reverse the list for deterministic
     * output without exposing readdir's filesystem order. */
    {
        NplVal reversed = npl_nil();
        npl_gc_root(&reversed);
        while (!npl_is_nil(result)) {
            reversed = npl_cons(npl_car(result), reversed);
            result = npl_cdr(result);
        }
        head = reversed;
        npl_gc_unroot(&reversed);
    }
    npl_gc_unroot(&result);
    return head;
}

NplVal npl_unix_open_directory(NplVal path_value)
{
    char *path = unix_path(path_value, "open-directory");
    DIR *directory = opendir(path);
    free(path);
    if (!directory) unix_raise("open-directory", errno, path_value);
    return npl_pointer(directory);
}

NplVal npl_unix_read_directory(NplVal handle, NplVal dotfiles_value)
{
    DIR *directory = (DIR *)npl_pointer_address(handle, "read-directory");
    struct dirent *entry;
    int include_dotfiles = npl_is_true(dotfiles_value);
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;
        if (!include_dotfiles && unix_dotfile(entry->d_name))
            continue;
        return npl_string(entry->d_name, (uint32_t)strlen(entry->d_name));
    }
    return npl_eof_object();
}

NplVal npl_unix_close_directory(NplVal handle)
{
    DIR *directory = (DIR *)npl_pointer_address(handle, "close-directory");
    closedir(directory);
    return npl_unspecified();
}

static NplVal unix_fixnum_result(const char *who, int status, int code,
                                 NplVal irritant)
{
    if (status != 0) unix_raise(who, code, irritant);
    return npl_unspecified();
}

NplVal npl_unix_create_directory(NplVal path, NplVal mode)
{
    char *name = unix_path(path, "create-directory");
    int status = mkdir(name, (mode_t)npl_fixnum_value(mode));
    int code = errno;
    free(name);
    return unix_fixnum_result("create-directory", status, code, path);
}

NplVal npl_unix_create_fifo(NplVal path, NplVal mode)
{
    char *name = unix_path(path, "create-fifo");
    int status = mkfifo(name, (mode_t)npl_fixnum_value(mode));
    int code = errno;
    free(name);
    return unix_fixnum_result("create-fifo", status, code, path);
}

NplVal npl_unix_create_hard_link(NplVal old_path, NplVal new_path)
{
    char *old_name = unix_path(old_path, "create-hard-link");
    char *new_name = unix_path(new_path, "create-hard-link");
    int status = link(old_name, new_name);
    int code = errno;
    free(old_name);
    free(new_name);
    return unix_fixnum_result("create-hard-link", status, code, old_path);
}

NplVal npl_unix_create_symlink(NplVal old_path, NplVal new_path)
{
    char *old_name = unix_path(old_path, "create-symlink");
    char *new_name = unix_path(new_path, "create-symlink");
    int status = symlink(old_name, new_name);
    int code = errno;
    free(old_name);
    free(new_name);
    return unix_fixnum_result("create-symlink", status, code, old_path);
}

NplVal npl_unix_read_symlink(NplVal path_value)
{
    char *path = unix_path(path_value, "read-symlink");
    char buffer[PATH_MAX + 1];
    ssize_t length = readlink(path, buffer, PATH_MAX);
    int code = errno;
    free(path);
    if (length < 0) unix_raise("read-symlink", code, path_value);
    buffer[length] = '\0';
    return npl_string(buffer, (uint32_t)length);
}

NplVal npl_unix_rename_file(NplVal old_path, NplVal new_path)
{
    char *old_name = unix_path(old_path, "rename-file");
    char *new_name = unix_path(new_path, "rename-file");
    int status = rename(old_name, new_name);
    int code = errno;
    free(old_name);
    free(new_name);
    return unix_fixnum_result("rename-file", status, code, old_path);
}

NplVal npl_unix_set_file_owner(NplVal path, NplVal uid, NplVal gid)
{
    char *name = unix_path(path, "set-file-owner");
    int status = chown(name, (uid_t)npl_fixnum_value(uid),
                       (gid_t)npl_fixnum_value(gid));
    int code = errno;
    free(name);
    return unix_fixnum_result("set-file-owner", status, code, path);
}

NplVal npl_unix_set_file_mode(NplVal path, NplVal mode)
{
    char *name = unix_path(path, "set-file-mode");
    int status = chmod(name, (mode_t)npl_fixnum_value(mode));
    int code = errno;
    free(name);
    return unix_fixnum_result("set-file-mode", status, code, path);
}

NplVal npl_unix_set_file_times(NplVal path, NplVal access_sec,
                               NplVal access_nsec, NplVal modify_sec,
                               NplVal modify_nsec)
{
    char *name = unix_path(path, "set-file-times");
    struct timespec times[2];
    long access_seconds = (long)npl_fixnum_value(access_sec);
    long modify_seconds = (long)npl_fixnum_value(modify_sec);
    times[0].tv_sec = (time_t)(access_seconds == -2 ? 0 : access_seconds);
    times[0].tv_nsec = access_seconds == -2 ? UTIME_NOW
                     : access_seconds == -1 ? UTIME_OMIT
                     : (long)npl_fixnum_value(access_nsec);
    times[1].tv_sec = (time_t)(modify_seconds == -2 ? 0 : modify_seconds);
    times[1].tv_nsec = modify_seconds == -2 ? UTIME_NOW
                     : modify_seconds == -1 ? UTIME_OMIT
                     : (long)npl_fixnum_value(modify_nsec);
    int status = utimensat(AT_FDCWD, name, times, 0);
    int code = errno;
    free(name);
    return unix_fixnum_result("set-file-times", status, code, path);
}

NplVal npl_unix_truncate_file(NplVal path, NplVal length)
{
    char *name = unix_path(path, "truncate-file");
    int status = truncate(name, (off_t)npl_fixnum_value(length));
    int code = errno;
    free(name);
    return unix_fixnum_result("truncate-file", status, code, path);
}

NplVal npl_unix_truncate_port(NplVal port, NplVal length)
{
    NplVal fd = npl_port_fileno(port);
    if (npl_is_false(fd))
        npl_primitive_error("truncate-file", "expected a file port", port);
    if (ftruncate((int)npl_fixnum_value(fd),
                  (off_t)npl_fixnum_value(length)) != 0)
        unix_raise("truncate-file", errno, port);
    return npl_unspecified();
}

NplVal npl_unix_real_path(NplVal path_value)
{
    char *path = unix_path(path_value, "real-path");
    char *resolved = realpath(path, NULL);
    int code = errno;
    free(path);
    if (!resolved) unix_raise("real-path", code, path_value);
    {
        NplVal result = npl_string(resolved, (uint32_t)strlen(resolved));
        free(resolved);
        return result;
    }
}

NplVal npl_unix_file_space_port(NplVal port)
{
    NplVal fd = npl_port_fileno(port);
    struct statvfs info;
    if (npl_is_false(fd))
        npl_primitive_error("file-space", "expected a file port", port);
    if (fstatvfs((int)npl_fixnum_value(fd), &info) != 0)
        unix_raise("file-space", errno, port);
    return npl_fixnum((int64_t)info.f_bavail * (int64_t)info.f_frsize);
}

NplVal npl_unix_file_space(NplVal path_value)
{
    char *path = unix_path(path_value, "file-space");
    struct statvfs info;
    int status = statvfs(path, &info);
    int code = errno;
    free(path);
    if (status != 0) unix_raise("file-space", code, path_value);
    return npl_fixnum((int64_t)info.f_bavail * (int64_t)info.f_frsize);
}

NplVal npl_unix_create_temp_file(NplVal prefix_value)
{
    char *prefix = unix_path(prefix_value, "create-temp-file");
    size_t length = strlen(prefix);
    char *template = (char *)malloc(length + 7);
    int descriptor;
    if (!template) npl_runtime_oom_internal();
    memcpy(template, prefix, length);
    memcpy(template + length, "XXXXXX", 7);
    descriptor = mkstemp(template);
    if (descriptor < 0) {
        int code = errno;
        free(prefix);
        unix_raise("create-temp-file", code, prefix_value);
    }
    close(descriptor);
    {
        NplVal result = npl_string(template, (uint32_t)strlen(template));
        free(template);
        free(prefix);
        return result;
    }
}

NplVal npl_unix_umask(void)
{
    mode_t mask = umask(0);
    umask(mask);
    return npl_fixnum((int64_t)mask);
}

NplVal npl_unix_set_umask(NplVal mask)
{
    umask((mode_t)npl_fixnum_value(mask));
    return npl_unspecified();
}

NplVal npl_unix_current_directory(void)
{
    char buffer[PATH_MAX + 1];
    if (!getcwd(buffer, sizeof buffer))
        unix_raise("current-directory", errno, npl_nil());
    return npl_string(buffer, (uint32_t)strlen(buffer));
}

NplVal npl_unix_set_current_directory(NplVal path)
{
    char *name = unix_path(path, "set-current-directory!");
    int status = chdir(name);
    int code = errno;
    free(name);
    return unix_fixnum_result("set-current-directory!", status, code, path);
}

NplVal npl_unix_pid(void)
{
    return npl_fixnum((int64_t)getpid());
}

NplVal npl_unix_nice(NplVal delta)
{
    int value = nice((int)npl_fixnum_value(delta));
    if (value == -1 && errno != 0)
        unix_raise("nice", errno, delta);
    return npl_fixnum(value);
}

NplVal npl_unix_getuid(void) { return npl_fixnum((int64_t)getuid()); }
NplVal npl_unix_getgid(void) { return npl_fixnum((int64_t)getgid()); }
NplVal npl_unix_geteuid(void) { return npl_fixnum((int64_t)geteuid()); }
NplVal npl_unix_getegid(void) { return npl_fixnum((int64_t)getegid()); }

NplVal npl_unix_getgroups(void)
{
    int count = getgroups(0, NULL);
    gid_t *groups;
    NplVal result = npl_nil();
    if (count < 0) unix_raise("user-supplementary-gids", errno, npl_nil());
    groups = (gid_t *)malloc(sizeof(gid_t) * (size_t)count);
    if (!groups) npl_runtime_oom_internal();
    if (getgroups(count, groups) < 0) {
        int code = errno;
        free(groups);
        unix_raise("user-supplementary-gids", code, npl_nil());
    }
    npl_gc_root(&result);
    while (count > 0) {
        count--;
        result = npl_cons(npl_fixnum((int64_t)groups[count]), result);
    }
    free(groups);
    npl_gc_unroot(&result);
    return result;
}

static NplVal unix_string_or_empty(const char *text)
{
    if (!text) return npl_false();
    return npl_string(text, (uint32_t)strlen(text));
}

NplVal npl_unix_user_info(NplVal key)
{
    struct passwd *entry;
    NplVal fields[6];
    if (npl_is_fixnum(key))
        entry = getpwuid((uid_t)npl_fixnum_value(key));
    else {
        char *name = unix_path(key, "user-info");
        entry = getpwnam(name);
        free(name);
    }
    if (!entry) return npl_false();
    fields[0] = unix_string_or_empty(entry->pw_name);
    fields[1] = npl_fixnum((int64_t)entry->pw_uid);
    fields[2] = npl_fixnum((int64_t)entry->pw_gid);
    fields[3] = unix_string_or_empty(entry->pw_dir);
    fields[4] = unix_string_or_empty(entry->pw_shell);
#if defined(__APPLE__)
    fields[5] = unix_string_or_empty(entry->pw_gecos);
#else
    fields[5] = unix_string_or_empty(entry->pw_gecos);
#endif
    return npl_make_vector(6, fields);
}

NplVal npl_unix_group_info(NplVal key)
{
    struct group *entry;
    NplVal fields[2];
    if (npl_is_fixnum(key))
        entry = getgrgid((gid_t)npl_fixnum_value(key));
    else {
        char *name = unix_path(key, "group-info");
        entry = getgrnam(name);
        free(name);
    }
    if (!entry) return npl_false();
    fields[0] = unix_string_or_empty(entry->gr_name);
    fields[1] = npl_fixnum((int64_t)entry->gr_gid);
    return npl_make_vector(2, fields);
}

NplVal npl_unix_time(NplVal kind)
{
    struct timespec value;
    clockid_t clock = npl_fixnum_value(kind) ? CLOCK_MONOTONIC
                                             : CLOCK_REALTIME;
    NplVal fields[2];
    if (clock_gettime(clock, &value) != 0)
        unix_raise("time", errno, kind);
    fields[0] = npl_fixnum((int64_t)value.tv_sec);
    fields[1] = npl_fixnum((int64_t)value.tv_nsec);
    return npl_make_vector(2, fields);
}

NplVal npl_unix_set_environment(NplVal name_value, NplVal value_value)
{
    char *name = unix_path(name_value, "set-environment-variable!");
    char *value = unix_path(value_value, "set-environment-variable!");
    int status = setenv(name, value, 1);
    int code = errno;
    free(name);
    free(value);
    return unix_fixnum_result("set-environment-variable!", status, code,
                              name_value);
}

NplVal npl_unix_delete_environment(NplVal name_value)
{
    char *name = unix_path(name_value, "delete-environment-variable!");
    int status = unsetenv(name);
    int code = errno;
    free(name);
    return unix_fixnum_result("delete-environment-variable!", status, code,
                              name_value);
}

static int unix_symbol_is(NplVal value, const char *text)
{
    const NplString *string;
    if (!npl_is_ptr(value) ||
        (npl_header(value)->tag != BOX_SYMBOL &&
         npl_header(value)->tag != BOX_STRING))
        return 0;
    string = (const NplString *)npl_header(value);
    return string->length == (uint32_t)strlen(text) &&
           memcmp(string->data, text, string->length) == 0;
}

NplVal npl_unix_open_file(NplVal path_value, NplVal kind_value,
                          NplVal flags_value, NplVal mode_value,
                          NplVal buffering_value)
{
    char *path = unix_path(path_value, "open-file");
    int input = 0, output = 0, textual = 0;
    int flags = 0;
    int descriptor;
    NplVal rest;
    if (unix_symbol_is(kind_value, "binary-input") ||
        unix_symbol_is(kind_value, "textual-input")) {
        input = 1;
        flags |= O_RDONLY;
        textual = unix_symbol_is(kind_value, "textual-input");
    } else if (unix_symbol_is(kind_value, "binary-output") ||
               unix_symbol_is(kind_value, "textual-output")) {
        output = 1;
        flags |= O_WRONLY;
        textual = unix_symbol_is(kind_value, "textual-output");
    } else if (unix_symbol_is(kind_value, "binary-input/output") ||
               unix_symbol_is(kind_value, "textual-input/output")) {
        input = 1;
        output = 1;
        flags |= O_RDWR;
        textual = unix_symbol_is(kind_value, "textual-input/output");
    } else {
        free(path);
        npl_primitive_error("open-file", "invalid port type", kind_value);
    }
    if (npl_is_fixnum(flags_value)) {
        int bits = (int)npl_fixnum_value(flags_value);
        if (bits & 1) flags |= O_APPEND;
        if (bits & 2) flags |= O_CREAT;
        if (bits & 4) flags |= O_EXCL;
        if (bits & 8) flags |= O_NOFOLLOW;
        if (bits & 16) flags |= O_TRUNC;
    } else {
        for (rest = flags_value; !npl_is_nil(rest); rest = npl_cdr(rest)) {
            NplVal flag = npl_car(rest);
            if (unix_symbol_is(flag, "open/append")) flags |= O_APPEND;
            else if (unix_symbol_is(flag, "open/create")) flags |= O_CREAT;
            else if (unix_symbol_is(flag, "open/exclusive")) flags |= O_EXCL;
            else if (unix_symbol_is(flag, "open/nofollow")) flags |= O_NOFOLLOW;
            else if (unix_symbol_is(flag, "open/truncate")) flags |= O_TRUNC;
            else if (unix_symbol_is(flag, "open/read")) flags |= O_RDONLY;
            else if (unix_symbol_is(flag, "open/write")) flags |= O_WRONLY;
        }
    }
    descriptor = open(path, flags, (mode_t)npl_fixnum_value(mode_value));
    {
        int code = errno;
        free(path);
        if (descriptor < 0) unix_raise("open-file", code, path_value);
    }
    return npl_port_from_fd(descriptor, input, output, textual);
}

NplVal npl_unix_fd_port(NplVal fd_value, NplVal kind_value)
{
    int input = 0, output = 0, textual = 0;
    if (unix_symbol_is(kind_value, "binary-input") ||
        unix_symbol_is(kind_value, "textual-input")) {
        input = 1;
        textual = unix_symbol_is(kind_value, "textual-input");
    } else if (unix_symbol_is(kind_value, "binary-output") ||
               unix_symbol_is(kind_value, "textual-output")) {
        output = 1;
        textual = unix_symbol_is(kind_value, "textual-output");
    } else if (unix_symbol_is(kind_value, "binary-input/output") ||
               unix_symbol_is(kind_value, "textual-input/output")) {
        input = 1;
        output = 1;
        textual = unix_symbol_is(kind_value, "textual-input/output");
    } else {
        npl_primitive_error("fd->port", "invalid port type", kind_value);
    }
    return npl_port_from_fd((int)npl_fixnum_value(fd_value), input, output,
                            textual);
}

NplVal npl_unix_terminal_p(NplVal port)
{
    NplVal fd = npl_port_fileno(port);
    int status;
    if (npl_is_false(fd)) return npl_false();
    status = isatty((int)npl_fixnum_value(fd));
    if (status == 0 && errno != ENOTTY)
        unix_raise("terminal?", errno, port);
    return status ? npl_true() : npl_false();
}
