# POSIX (SRFI 170)

`(npl unix)` / `(srfi 170)` is a core native package: the C entry points
live in `lib/npl/unix/native/unix.c`, CMake builds `libnpl_unix`, and `nplc`
and `npli` link it automatically. Importing the library needs no `-l` flag.

## Implemented

- Errors: `posix-error?`, `posix-error-name`, `posix-error-message`.
- I/O: `open-file`, `fd->port`, `port-fileno` (NPL extension), the
  `binary-*` / `textual-*` port-type values, `buffer-none`,
  `buffer-block`, `buffer-line`, and the `open/append`, `open/create`,
  `open/exclusive`, `open/nofollow`, `open/truncate` integer flags.
- File system: `create-directory`, `create-fifo`, `create-hard-link`,
  `create-symlink`, `read-symlink`, `rename-file`, `delete-directory`,
  `set-file-owner`, `set-file-times`, `truncate-file`, `file-info`,
  `file-info?` and its accessors/predicates, `set-file-mode`,
  `directory-files`, `make-directory-files-generator`, `open-directory`,
  `read-directory`, `close-directory`, `real-path`, `file-space`,
  `temp-file-prefix`, `create-temp-file`, `call-with-temporary-filename`.
- Process: `umask`, `set-umask!`, `current-directory`,
  `set-current-directory!`, `pid`, `nice`, `user-uid`, `user-gid`,
  `user-effective-uid`, `user-effective-gid`, `user-supplementary-gids`.
- Databases: `user-info` and accessors (including
  `user-info:parsed-full-name`), `group-info` and accessors.
- Time: `posix-time` returns `time-utc`; `monotonic-time` returns
  `time-monotonic`.
- Environment: the R7RS getters plus `set-environment-variable!` and
  `delete-environment-variable!`.
- Terminal: `terminal?`.

`file-info` returns a record; times are `time-utc` values. `set-file-times`
accepts `time/now` and `time/unchanged` constants as well as `time-utc`
values; every second value is an ordinary timestamp (-1 is
1969-12-31 23:59:59, not "unchanged"). Seconds and `truncate-file` lengths
cover the full signed 64-bit range; a host that cannot store a far timestamp
or length clamps it or raises a posix error. `owner/unchanged` and
`group/unchanged` leave ownership alone.

`open-directory` returns a directory object; `read-directory` and
`close-directory` accept only such objects (a foreign pointer or any other
value raises), and a closed object stays closed.

`delete-directory` is one binding shared with `(npl process)`; through either
library it raises a posix error (`posix-error?` is true, `file-error?` is
not).

```scheme
(import (scheme base) (npl unix))

(let ((info (file-info "README.md" #t)))
  (list (file-info:size info) (file-info-regular? info)))
```

## Deviations and limits

- A POSIX failure is an `npl-error-object` with kind `posix`; the name is the
  symbolic `errno` (`ENOENT`, `EACCES`, ...). The numeric `errno`, procedure
  name, C function, and argument list are not exposed separately.
- `open-file` accepts the integer flag bitmask 0..31 (`open/append` through
  `open/truncate`); unknown bits and out-of-range values are rejected before
  the host call. `buffer-block` is the default; the buffer-mode argument is
  accepted but currently left to the host's `fdopen` default.
- `fd->port` wraps an already open descriptor and takes ownership. It does
  not yet detect that another port already owns the same descriptor.
- `file-info` on a port ignores the follow flag, as SRFI 170 specifies.
- `directory-files` and `read-directory` return host directory order;
  callers that need a stable order should sort.
- `call-with-temporary-filename` retries by creating and removing a unique
  name before calling the maker; it is not a security boundary for
  adversarial concurrent processes.
- `(srfi 170)` is an alias for `(npl unix)`.

Tests: `srfi-170` (119 checks, all 97 exports referenced) and the nplc/npli `unix` fixture.
