/*
 * os-win32.c — Windows host primitives for runtime/ (compiled only on _WIN32).
 */
#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "npl.h"
#include "os-win32.h"

/* ---- UTF-8 / UTF-16 helpers ---- */

static wchar_t *utf8_to_wide(const char *text)
{
    int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (needed <= 0) return NULL;
    wchar_t *out = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (!out) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, out, needed) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

static char *wide_to_utf8(const wchar_t *text)
{
    int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) return NULL;
    char *out = (char *)malloc((size_t)needed);
    if (!out) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, out, needed, NULL, NULL) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

static void normalize_slashes(char *path)
{
    for (char *p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
}

/* ---- Memory & stderr ---- */

void *npl_win32_map_chunk(size_t size)
{
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void npl_win32_write_stderr(const char *msg, size_t len)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    if (h && h != INVALID_HANDLE_VALUE)
        (void)WriteFile(h, msg, (DWORD)len, &written, NULL);
}

void npl_win32_stack_bounds(uintptr_t *low, uintptr_t *high)
{
    ULONG_PTR lo = 0;
    ULONG_PTR hi = 0;
    GetCurrentThreadStackLimits(&lo, &hi);
    *low = (uintptr_t)lo;
    *high = (uintptr_t)hi;
}

/* ---- Child process spawning ---- */

static int needs_quoting(const char *arg)
{
    if (arg[0] == '\0') return 1;
    for (const char *p = arg; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
            *p == '\v' || *p == '\f' || *p == '"')
            return 1;
    }
    return 0;
}

static int append_char(char **buf, size_t *len, size_t *cap, char ch)
{
    if (*len + 2 > *cap) {
        size_t next = *cap ? (*cap * 2) : 256;
        char *grown = (char *)realloc(*buf, next);
        if (!grown) return 0;
        *buf = grown;
        *cap = next;
    }
    (*buf)[(*len)++] = ch;
    (*buf)[*len] = '\0';
    return 1;
}

/* Format argv according to Windows CommandLineToArgvW quoting rules. */
static char *build_command_line(char *const *argv)
{
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    for (size_t i = 0; argv[i]; i++) {
        if (i > 0 && !append_char(&buf, &len, &cap, ' ')) goto fail;
        const char *arg = argv[i];
        if (!needs_quoting(arg)) {
            for (const char *p = arg; *p; p++) {
                if (!append_char(&buf, &len, &cap, *p)) goto fail;
            }
            continue;
        }
        if (!append_char(&buf, &len, &cap, '"')) goto fail;
        size_t backslashes = 0;
        for (const char *p = arg; *p; p++) {
            if (*p == '\\') {
                backslashes++;
            } else if (*p == '"') {
                for (size_t k = 0; k < backslashes * 2 + 1; k++) {
                    if (!append_char(&buf, &len, &cap, '\\')) goto fail;
                }
                if (!append_char(&buf, &len, &cap, '"')) goto fail;
                backslashes = 0;
            } else {
                for (size_t k = 0; k < backslashes; k++) {
                    if (!append_char(&buf, &len, &cap, '\\')) goto fail;
                }
                backslashes = 0;
                if (!append_char(&buf, &len, &cap, *p)) goto fail;
            }
        }
        for (size_t k = 0; k < backslashes * 2; k++) {
            if (!append_char(&buf, &len, &cap, '\\')) goto fail;
        }
        if (!append_char(&buf, &len, &cap, '"')) goto fail;
    }
    if (!buf) {
        buf = (char *)malloc(1);
        if (buf) buf[0] = '\0';
    }
    return buf;
fail:
    free(buf);
    return NULL;
}

static HANDLE open_redirect_handle(const char *path, int for_write)
{
    wchar_t *wpath = utf8_to_wide(path);
    if (!wpath) return INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE h = CreateFileW(
        wpath,
        for_write ? GENERIC_WRITE : GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        &sa,
        for_write ? CREATE_ALWAYS : OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    free(wpath);
    return h;
}

static HANDLE inheritable_std_handle(DWORD which, int *must_close, int *failed)
{
    HANDLE orig = GetStdHandle(which);
    *must_close = 0;
    *failed = 0;
    if (!orig || orig == INVALID_HANDLE_VALUE) return orig;
    HANDLE dup = INVALID_HANDLE_VALUE;
    if (DuplicateHandle(GetCurrentProcess(), orig, GetCurrentProcess(), &dup,
                        0, TRUE, DUPLICATE_SAME_ACCESS)) {
        *must_close = 1;
        return dup;
    }
    *failed = 1;
    return INVALID_HANDLE_VALUE;
}

int npl_win32_spawn_wait(char *const *argv,
                         const char *input_path,
                         const char *output_path,
                         const char *error_path,
                         int *exit_code)
{
    char *cmdline = build_command_line(argv);
    if (!cmdline) return -2;
    wchar_t *wcmd = utf8_to_wide(cmdline);
    free(cmdline);
    if (!wcmd) return -2;

    HANDLE h_in = INVALID_HANDLE_VALUE;
    HANDLE h_out = INVALID_HANDLE_VALUE;
    HANDLE h_err = INVALID_HANDLE_VALUE;
    int close_in = 0, close_out = 0, close_err = 0;
    int dup_failed = 0;

    if (input_path) {
        h_in = open_redirect_handle(input_path, 0);
        if (h_in == INVALID_HANDLE_VALUE) goto redirect_fail;
        close_in = 1;
    } else {
        h_in = inheritable_std_handle(STD_INPUT_HANDLE, &close_in, &dup_failed);
        if (dup_failed) goto redirect_fail;
    }
    if (output_path) {
        h_out = open_redirect_handle(output_path, 1);
        if (h_out == INVALID_HANDLE_VALUE) goto redirect_fail;
        close_out = 1;
    } else {
        h_out = inheritable_std_handle(STD_OUTPUT_HANDLE, &close_out, &dup_failed);
        if (dup_failed) goto redirect_fail;
    }
    if (error_path) {
        h_err = open_redirect_handle(error_path, 1);
        if (h_err == INVALID_HANDLE_VALUE) goto redirect_fail;
        close_err = 1;
    } else {
        h_err = inheritable_std_handle(STD_ERROR_HANDLE, &close_err, &dup_failed);
        if (dup_failed) goto redirect_fail;
    }

    HANDLE inherit_list[3];
    DWORD inherit_count = 0;
    if (h_in && h_in != INVALID_HANDLE_VALUE) inherit_list[inherit_count++] = h_in;
    if (h_out && h_out != INVALID_HANDLE_VALUE) inherit_list[inherit_count++] = h_out;
    if (h_err && h_err != INVALID_HANDLE_VALUE) inherit_list[inherit_count++] = h_err;

    STARTUPINFOEXW siex;
    memset(&siex, 0, sizeof(siex));
    siex.StartupInfo.cb = sizeof(siex.StartupInfo);
    siex.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    siex.StartupInfo.hStdInput = h_in;
    siex.StartupInfo.hStdOutput = h_out;
    siex.StartupInfo.hStdError = h_err;

    LPPROC_THREAD_ATTRIBUTE_LIST attr_list = NULL;
    DWORD creation_flags = 0;
    if (inherit_count > 0) {
        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
        if (attr_size > 0) {
            attr_list = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attr_size);
            if (attr_list &&
                InitializeProcThreadAttributeList(attr_list, 1, 0, &attr_size)) {
                if (UpdateProcThreadAttribute(
                        attr_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                        inherit_list, inherit_count * sizeof(HANDLE),
                        NULL, NULL)) {
                    siex.StartupInfo.cb = sizeof(siex);
                    siex.lpAttributeList = attr_list;
                    creation_flags = EXTENDED_STARTUPINFO_PRESENT;
                } else {
                    DeleteProcThreadAttributeList(attr_list);
                    free(attr_list);
                    attr_list = NULL;
                }
            } else {
                free(attr_list);
                attr_list = NULL;
            }
        }
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL,
                             inherit_count > 0 ? TRUE : FALSE,
                             creation_flags, NULL, NULL,
                             &siex.StartupInfo, &pi);
    if (attr_list) {
        DeleteProcThreadAttributeList(attr_list);
        free(attr_list);
    }
    free(wcmd);
    if (close_in && h_in != INVALID_HANDLE_VALUE) CloseHandle(h_in);
    if (close_out && h_out != INVALID_HANDLE_VALUE) CloseHandle(h_out);
    if (close_err && h_err != INVALID_HANDLE_VALUE) CloseHandle(h_err);

    if (!ok) return -1;

    npl_gc_block_begin();
    DWORD wait_res = WaitForSingleObject(pi.hProcess, INFINITE);
    npl_gc_block_end();

    DWORD code = 1;
    if (wait_res == WAIT_OBJECT_0)
        (void)GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (wait_res != WAIT_OBJECT_0) return -3;
    *exit_code = (int)code;
    return 0;

redirect_fail:
    free(wcmd);
    if (close_in && h_in != INVALID_HANDLE_VALUE) CloseHandle(h_in);
    if (close_out && h_out != INVALID_HANDLE_VALUE) CloseHandle(h_out);
    if (close_err && h_err != INVALID_HANDLE_VALUE) CloseHandle(h_err);
    return -2;
}

/* ---- Temporary directory & executable path ---- */

char *npl_win32_make_temp_dir(char *root_buf, size_t root_cap)
{
    char temp_root[MAX_PATH * 3];
    const char *root = getenv("TMPDIR");
    if (!root || root[0] == '\0') root = getenv("TEMP");
    if (!root || root[0] == '\0') root = getenv("TMP");
    if (!root || root[0] == '\0') {
        wchar_t wtmp[MAX_PATH + 1];
        DWORD n = GetTempPathW(MAX_PATH + 1, wtmp);
        if (n > 0 && n <= MAX_PATH) {
            char *u = wide_to_utf8(wtmp);
            if (u) {
                strncpy(temp_root, u, sizeof(temp_root) - 1);
                temp_root[sizeof(temp_root) - 1] = '\0';
                free(u);
                root = temp_root;
            }
        }
    }
    if (!root || root[0] == '\0') root = ".";
    if (root_buf && root_cap > 0) {
        strncpy(root_buf, root, root_cap - 1);
        root_buf[root_cap - 1] = '\0';
    }

    size_t rlen = strlen(root);
    while (rlen > 1 && (root[rlen - 1] == '/' || root[rlen - 1] == '\\'))
        rlen--;

    static unsigned long counter = 0;
    DWORD pid = GetCurrentProcessId();
    for (int attempt = 0; attempt < 256; attempt++) {
        unsigned long seq = (unsigned long)__sync_fetch_and_add(&counter, 1);
        DWORD tick = GetTickCount();
        size_t cap = rlen + 64;
        char *candidate = (char *)malloc(cap);
        if (!candidate) return NULL;
        snprintf(candidate, cap, "%.*s/npl-driver.%lu.%lu.%lu",
                 (int)rlen, root, (unsigned long)pid,
                 (unsigned long)tick, seq);
        normalize_slashes(candidate);
        wchar_t *wpath = utf8_to_wide(candidate);
        if (!wpath) {
            free(candidate);
            return NULL;
        }
        BOOL created = CreateDirectoryW(wpath, NULL);
        DWORD err = GetLastError();
        free(wpath);
        if (created) return candidate;
        free(candidate);
        if (err != ERROR_ALREADY_EXISTS) return NULL;
    }
    return NULL;
}

int npl_win32_rmdir(const char *path)
{
    wchar_t *wpath = utf8_to_wide(path);
    if (!wpath) {
        errno = EINVAL;
        return -1;
    }
    int rc = _wrmdir(wpath);
    int saved = errno;
    free(wpath);
    errno = saved;
    return rc;
}

char *npl_win32_executable_path(void)
{
    DWORD cap = MAX_PATH;
    for (;;) {
        wchar_t *wbuf = (wchar_t *)malloc((size_t)cap * sizeof(wchar_t));
        if (!wbuf) return NULL;
        DWORD n = GetModuleFileNameW(NULL, wbuf, cap);
        if (n == 0) {
            free(wbuf);
            return NULL;
        }
        if (n < cap - 1) {
            char *utf8 = wide_to_utf8(wbuf);
            free(wbuf);
            if (utf8) normalize_slashes(utf8);
            return utf8;
        }
        free(wbuf);
        if (cap > 32768) return NULL;
        cap *= 2;
    }
}

/* ---- Dynamic libraries for FFI ---- */

int npl_win32_dlopen(const char *path, char *error_buffer, size_t error_size)
{
    wchar_t *wpath = utf8_to_wide(path);
    if (!wpath) {
        snprintf(error_buffer, error_size, "invalid library path");
        return 0;
    }
    HMODULE mod = LoadLibraryW(wpath);
    DWORD err = GetLastError();
    free(wpath);
    if (mod) return 1;
    snprintf(error_buffer, error_size, "LoadLibrary failed (error %lu)",
             (unsigned long)err);
    return 0;
}

void *npl_win32_dlsym_default(const char *symbol)
{
    HMODULE self = GetModuleHandleW(NULL);
    if (self) {
        FARPROC p = GetProcAddress(self, symbol);
        if (p) {
            void *addr = NULL;
            memcpy(&addr, &p, sizeof(addr));
            return addr;
        }
    }
    HMODULE mods[512];
    DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        DWORD count = needed / (DWORD)sizeof(HMODULE);
        if (count > 512) count = 512;
        for (DWORD i = 0; i < count; i++) {
            FARPROC p = GetProcAddress(mods[i], symbol);
            if (p) {
                void *addr = NULL;
                memcpy(&addr, &p, sizeof(addr));
                return addr;
            }
        }
    }
    return NULL;
}

/* ---- Stream readiness & watchdog ---- */

void npl_win32_prepare_input_stream(FILE *stream)
{
    int fd = _fileno(stream);
    if (fd < 0) return;
    intptr_t raw = _get_osfhandle(fd);
    if (raw == -1) return;
    DWORD type = GetFileType((HANDLE)raw);
    if (type == FILE_TYPE_PIPE || type == FILE_TYPE_CHAR)
        (void)setvbuf(stream, NULL, _IONBF, 0);
}

static int console_has_readable_input(HANDLE h, DWORD mode)
{
    DWORD count = 0;
    if (!GetNumberOfConsoleInputEvents(h, &count) || count == 0)
        return 0;
    INPUT_RECORD stack_buf[64];
    INPUT_RECORD *records = stack_buf;
    if (count > 64) {
        records = (INPUT_RECORD *)malloc((size_t)count * sizeof(INPUT_RECORD));
        if (!records) return 0;
    }
    DWORD got = 0;
    int ready = 0;
    if (PeekConsoleInputW(h, records, count, &got)) {
        int line_mode = (mode & ENABLE_LINE_INPUT) != 0;
        for (DWORD i = 0; i < got; i++) {
            if (records[i].EventType != KEY_EVENT) continue;
            if (!records[i].Event.KeyEvent.bKeyDown) continue;
            wchar_t ch = records[i].Event.KeyEvent.uChar.UnicodeChar;
            if (line_mode) {
                if (ch == L'\r' || ch == L'\n' || ch == 0x1A) {
                    ready = 1;
                    break;
                }
            } else if (ch != 0) {
                ready = 1;
                break;
            }
        }
    }
    if (records != stack_buf) free(records);
    return ready;
}

int npl_win32_stream_ready(FILE *stream)
{
    int fd = _fileno(stream);
    if (fd < 0) return 0;
    intptr_t raw = _get_osfhandle(fd);
    if (raw == -1) return 0;
    HANDLE h = (HANDLE)raw;
    DWORD type = GetFileType(h);
    if (type == FILE_TYPE_PIPE) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
            return 1; /* Broken pipe / EOF will not block. */
        return avail > 0 ? 1 : 0;
    }
    if (type == FILE_TYPE_CHAR) {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode))
            return console_has_readable_input(h, mode);
        return WaitForSingleObject(h, 0) == WAIT_OBJECT_0 ? 1 : 0;
    }
    return 1;
}

static DWORD WINAPI alarm_watchdog_thread(LPVOID param)
{
    DWORD millis = (DWORD)(uintptr_t)param;
    Sleep(millis);
    static const char msg[] = "npl: watchdog alarm expired\n";
    npl_win32_write_stderr(msg, sizeof(msg) - 1);
    TerminateProcess(GetCurrentProcess(), 124);
    return 124;
}

void npl_win32_alarm(unsigned seconds)
{
    if (seconds == 0) return;
    DWORD millis = seconds > 4000000u ? 4000000000u : (DWORD)(seconds * 1000u);
    HANDLE thread = CreateThread(NULL, 0, alarm_watchdog_thread,
                                 (LPVOID)(uintptr_t)millis, 0, NULL);
    if (thread) CloseHandle(thread);
}

/* ---- Shared-DLL executable slot indirection ---- */

static NPL_TLS uintptr_t g_win_fallback_stack_limit = 0;
static uintptr_t g_win_fallback_gc_stop = 0;

static uintptr_t win_fallback_get_stack_limit(void)
{
    return g_win_fallback_stack_limit;
}

static void win_fallback_set_stack_limit(uintptr_t value)
{
    g_win_fallback_stack_limit = value;
}

static uintptr_t *g_win_gc_stop_ptr = &g_win_fallback_gc_stop;
static uintptr_t (*g_win_get_stack_limit)(void) = win_fallback_get_stack_limit;
static void (*g_win_set_stack_limit)(uintptr_t) = win_fallback_set_stack_limit;

void npl_win_register_slots(uintptr_t *gc_stop_ptr,
                            uintptr_t (*get_stack_limit)(void),
                            void (*set_stack_limit)(uintptr_t))
{
    if (gc_stop_ptr) g_win_gc_stop_ptr = gc_stop_ptr;
    if (get_stack_limit) g_win_get_stack_limit = get_stack_limit;
    if (set_stack_limit) g_win_set_stack_limit = set_stack_limit;
}

uintptr_t npl_win_get_stack_limit(void)
{
    return g_win_get_stack_limit();
}

void npl_win_set_stack_limit(uintptr_t value)
{
    g_win_set_stack_limit(value);
}

uintptr_t *npl_win_gc_stop_ptr(void)
{
    return g_win_gc_stop_ptr;
}

#endif /* _WIN32 */
