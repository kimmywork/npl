; SRFI 170 POSIX surface as a core native package.
;
; C entry points: lib/npl/unix/native/unix.c.  File metadata stays in
; vectors at the C boundary; this library builds SRFI 170 records and
; time-utc values, so callers never see C structs or pointers.
(define-library (npl unix)
  (export posix-error? posix-error-name posix-error-message
          binary-input textual-input binary-output textual-output
          binary-input/output buffer-none buffer-block buffer-line
          open-file fd->port
          create-directory create-fifo create-hard-link create-symlink
          read-symlink rename-file delete-directory
          set-file-owner set-file-times truncate-file
          file-info file-info? file-info:device file-info:inode
          file-info:mode file-info:nlinks file-info:uid file-info:gid
          file-info:rdev file-info:size file-info:blksize file-info:blocks
          file-info:atime file-info:mtime file-info:ctime
          file-info-directory? file-info-fifo? file-info-symlink?
          file-info-regular? file-info-socket? file-info-device?
          set-file-mode directory-files make-directory-files-generator
          open-directory read-directory close-directory
          real-path file-space temp-file-prefix create-temp-file port-fileno
          glob
          call-with-temporary-filename
          umask set-umask! current-directory set-current-directory!
          pid nice user-uid user-gid user-effective-uid user-effective-gid
          user-supplementary-gids
          user-info user-info? user-info:name user-info:uid user-info:gid
          user-info:home-dir user-info:shell user-info:full-name
          user-info:parsed-full-name
          group-info group-info? group-info:name group-info:gid
          posix-time monotonic-time
          get-environment-variables get-environment-variable
          set-environment-variable! delete-environment-variable!
          terminal?
          owner/unchanged group/unchanged time/now time/unchanged
          open/append open/create open/exclusive open/nofollow
          open/truncate)
  (import (scheme base)
          (scheme char)
          (scheme file)
          (scheme process-context)
          (only (npl process) delete-directory)
          (npl time)
          (npl data generator))
  (begin
    (native-package "npl_unix")

    (define-c-primitive (npl_unix_posix_error_p (value any)) boolean
      "npl_unix_posix_error_p")
    (define-c-primitive (npl_unix_posix_error_name (value any)) any
      "npl_unix_posix_error_name")
    (define-c-primitive (npl_unix_posix_error_message (value any)) any
      "npl_unix_posix_error_message")
    (define-c-primitive (npl_unix_file_info (path any) (follow any)) any
      "npl_unix_file_info")
    (define-c-primitive (npl_unix_file_info_port (port any)) any
      "npl_unix_file_info_port")
    (define-c-primitive (npl_unix_directory_files (path any) (dots any)) any
      "npl_unix_directory_files")
    (define-c-primitive (npl_unix_glob (pattern any)) any
      "npl_unix_glob")
    (define-c-primitive (npl_unix_open_directory (path any)) any
      "npl_unix_open_directory")
    (define-c-primitive (npl_unix_read_directory (handle any) (dots any)) any
      "npl_unix_read_directory")
    (define-c-primitive (npl_unix_close_directory (handle any)) any
      "npl_unix_close_directory")
    (define-c-primitive (npl_unix_create_directory (path any) (mode any)) any
      "npl_unix_create_directory")
    (define-c-primitive (npl_unix_create_fifo (path any) (mode any)) any
      "npl_unix_create_fifo")
    (define-c-primitive (npl_unix_create_hard_link (old any) (new any)) any
      "npl_unix_create_hard_link")
    (define-c-primitive (npl_unix_create_symlink (old any) (new any)) any
      "npl_unix_create_symlink")
    (define-c-primitive (npl_unix_read_symlink (path any)) any
      "npl_unix_read_symlink")
    (define-c-primitive (npl_unix_rename_file (old any) (new any)) any
      "npl_unix_rename_file")
    (define-c-primitive (npl_unix_set_file_owner
                         (path any) (uid any) (gid any)) any
      "npl_unix_set_file_owner")
    (define-c-primitive (npl_unix_set_file_mode (path any) (mode any)) any
      "npl_unix_set_file_mode")
    (define-c-primitive (npl_unix_set_file_times
                         (path any) (asec any) (ansec any)
                         (msec any) (mnsec any)) any
      "npl_unix_set_file_times")
    (define-c-primitive (npl_unix_truncate_file (path any) (len any)) any
      "npl_unix_truncate_file")
    (define-c-primitive (npl_unix_real_path (path any)) any
      "npl_unix_real_path")
    (define-c-primitive (npl_unix_file_space (path any)) any
      "npl_unix_file_space")
    (define-c-primitive (npl_unix_file_space_port (port any)) any
      "npl_unix_file_space_port")
    (define-c-primitive (npl_unix_truncate_port (port any) (len any)) any
      "npl_unix_truncate_port")
    (define-c-primitive (npl_unix_create_temp_file (prefix any)) any
      "npl_unix_create_temp_file")
    (define-c-primitive (npl_unix_umask) any "npl_unix_umask")
    (define-c-primitive (npl_unix_set_umask (mask any)) any
      "npl_unix_set_umask")
    (define-c-primitive (npl_unix_current_directory) any
      "npl_unix_current_directory")
    (define-c-primitive (npl_unix_set_current_directory (path any)) any
      "npl_unix_set_current_directory")
    (define-c-primitive (npl_unix_pid) any "npl_unix_pid")
    (define-c-primitive (npl_unix_nice (delta any)) any "npl_unix_nice")
    (define-c-primitive (npl_unix_getuid) any "npl_unix_getuid")
    (define-c-primitive (npl_unix_getgid) any "npl_unix_getgid")
    (define-c-primitive (npl_unix_geteuid) any "npl_unix_geteuid")
    (define-c-primitive (npl_unix_getegid) any "npl_unix_getegid")
    (define-c-primitive (npl_unix_getgroups) any "npl_unix_getgroups")
    (define-c-primitive (npl_unix_user_info (key any)) any
      "npl_unix_user_info")
    (define-c-primitive (npl_unix_group_info (key any)) any
      "npl_unix_group_info")
    (define-c-primitive (npl_unix_time (kind any)) any "npl_unix_time")
    (define-c-primitive (npl_unix_set_environment (name any) (value any)) any
      "npl_unix_set_environment")
    (define-c-primitive (npl_unix_delete_environment (name any)) any
      "npl_unix_delete_environment")
    (define-c-primitive (npl_unix_open_file
                         (path any) (kind any) (flags any) (mode any)
                         (buffer any)) any
      "npl_unix_open_file")
    (define-c-primitive (npl_unix_fd_port (fd any) (kind any)) any
      "npl_unix_fd_port")
    (define-c-primitive (npl_unix_terminal_p (port any)) boolean
      "npl_unix_terminal_p")
    (define-c-primitive (npl_unix_port_fileno (port any)) any
      "npl_port_fileno")

    (define owner/unchanged 'owner/unchanged)
    (define group/unchanged 'group/unchanged)
    (define time/now 'time/now)
    (define time/unchanged 'time/unchanged)

    (define binary-input 'binary-input)
    (define textual-input 'textual-input)
    (define binary-output 'binary-output)
    (define textual-output 'textual-output)
    (define binary-input/output 'binary-input/output)
    (define buffer-none 'buffer-none)
    (define buffer-block 'buffer-block)
    (define buffer-line 'buffer-line)
    ; Flag constants compose with +; the C entry point maps these bits to
    ; the host's O_* values.
    (define open/append 1)
    (define open/create 2)
    (define open/exclusive 4)
    (define open/nofollow 8)
    (define open/truncate 16)

    (define (require-integer name value)
      (if (exact-integer? value)
          value
          (error (string-append name ": expected an exact integer") value)))

    (define (require-nonnegative name value)
      (let ((value (require-integer name value)))
        (if (>= value 0)
            value
            (error (string-append name ": expected a non-negative integer")
                   value))))

    ; Permission masks fit mode_t's low 12 bits.
    (define (require-mode name value)
      (let ((value (require-nonnegative name value)))
        (if (<= value #o7777)
            value
            (error (string-append name ": permission bits out of range")
                   value))))

    ; uid_t and gid_t are unsigned 32-bit values on the supported hosts.
    (define (require-id name value)
      (let ((value (require-nonnegative name value)))
        (if (<= value 4294967295)
            value
            (error (string-append name ": id out of range") value))))

    ; off_t and time_t are signed 64-bit values here.
    (define (require-offset name value)
      (let ((value (require-integer name value)))
        (if (if (>= value 0) (<= value 9223372036854775807) #f)
            value
            (error (string-append name ": length out of range") value))))

    (define (require-int name value)
      (let ((value (require-integer name value)))
        (if (if (>= value -2147483648) (<= value 2147483647) #f)
            value
            (error (string-append name ": integer out of range") value))))

    ; time_t is a signed 64-bit value on the supported hosts.
    (define (require-time-second name value)
      (let ((value (require-integer name value)))
        (if (if (>= value -9223372036854775808)
                (<= value 9223372036854775807)
                #f)
            value
            (error (string-append name ": second out of range") value))))

    ; open/append through open/truncate occupy bits 0-4.
    (define (require-flags name value)
      (let ((value (require-integer name value)))
        (if (if (>= value 0) (<= value 31) #f)
            value
            (error (string-append name ": flags out of range") value))))

    (define (require-nanosecond name value)
      (let ((value (require-integer name value)))
        (if (if (>= value 0) (<= value 999999999) #f)
            value
            (error (string-append name ": nanosecond out of range") value))))

    (define (posix-error? object)
      (npl_unix_posix_error_p object))

    (define (posix-error-name object)
      (npl_unix_posix_error_name object))

    (define (posix-error-message object)
      (npl_unix_posix_error_message object))

    (define (open-file fname port-type flags . rest)
      (let ((mode (if (null? rest) #o666 (car rest)))
            (buffer (if (if (null? rest) #t (null? (cdr rest)))
                        buffer-block
                        (car (cdr rest)))))
        (require-mode "open-file" mode)
        (if (not (memq buffer (list buffer-none buffer-block buffer-line)))
            (error "open-file: invalid buffer mode" buffer))
        (npl_unix_open_file fname port-type
                            (require-flags "open-file" flags) mode buffer)))

    (define (fd->port fd port-type . rest)
      (npl_unix_fd_port fd port-type))

    (define (terminal? port)
      (npl_unix_terminal_p port))

    (define (port-fileno port)
      (npl_unix_port_fileno port))

    ; ---- file metadata ---------------------------------------------------

    (define-record-type file-info-record
      (make-file-info-record device inode mode nlinks uid gid rdev size
                             blksize blocks atime mtime ctime)
      file-info?
      (device file-info:device)
      (inode file-info:inode)
      (mode file-info:mode)
      (nlinks file-info:nlinks)
      (uid file-info:uid)
      (gid file-info:gid)
      (rdev file-info:rdev)
      (size file-info:size)
      (blksize file-info:blksize)
      (blocks file-info:blocks)
      (atime file-info:atime)
      (mtime file-info:mtime)
      (ctime file-info:ctime))

    (define (unix-time second nanosecond)
      (make-time 'time-utc nanosecond second))

    (define (file-info fname/port follow?)
      (let ((fields
              (if (string? fname/port)
                  (npl_unix_file_info fname/port follow?)
                  (npl_unix_file_info_port fname/port))))
        (make-file-info-record
          (vector-ref fields 0) (vector-ref fields 1)
          (vector-ref fields 2) (vector-ref fields 3)
          (vector-ref fields 4) (vector-ref fields 5)
          (vector-ref fields 6) (vector-ref fields 7)
          (vector-ref fields 8) (vector-ref fields 9)
          (unix-time (vector-ref fields 10) (vector-ref fields 11))
          (unix-time (vector-ref fields 12) (vector-ref fields 13))
          (unix-time (vector-ref fields 14) (vector-ref fields 15)))))

    (define (file-mode-kind value)
      (let ((mode (file-info:mode value)))
        (- mode (modulo mode #o10000))))

    (define (file-type? value kind)
      (if (file-info? value) (= (file-mode-kind value) kind) #f))

    (define (file-info-directory? value) (file-type? value #o40000))
    (define (file-info-fifo? value) (file-type? value #o10000))
    (define (file-info-symlink? value) (file-type? value #o120000))
    (define (file-info-regular? value) (file-type? value #o100000))
    (define (file-info-socket? value) (file-type? value #o140000))
    (define (file-info-device? value)
      (if (file-info? value)
          (let ((kind (file-mode-kind value)))
            (if (= kind #o20000) #t (= kind #o60000)))
          #f))

    ; ---- directories -----------------------------------------------------

    (define (glob pattern)
      (npl_unix_glob pattern))

    (define (directory-files directory . rest)
      (npl_unix_directory_files directory
                                (if (null? rest) #f (car rest))))

    ; A directory object wraps the C handle's pointer box.  The record type
    ; is private, so read/close accept only what open-directory returned:
    ; an arbitrary foreign pointer can never reach readdir/closedir (R23),
    ; and the handle's memory is not reachable through pointer-ref-*.
    (define-record-type directory-object
      (make-directory-object handle)
      directory-object?
      (handle directory-object-handle))

    (define (require-directory-object name value)
      (if (directory-object? value)
          (directory-object-handle value)
          (error (string-append name ": expected a directory object") value)))

    (define (open-directory directory . rest)
      (make-directory-object (npl_unix_open_directory directory)))

    (define (read-directory directory . rest)
      (npl_unix_read_directory
        (require-directory-object "read-directory" directory)
        (if (null? rest) #f (car rest))))

    (define (close-directory directory)
      (npl_unix_close_directory
        (require-directory-object "close-directory" directory)))

    (define (make-directory-files-generator directory . rest)
      (list->generator
        (apply directory-files directory rest)))

    ; ---- creation and mutation ------------------------------------------

    (define (create-directory fname . rest)
      (npl_unix_create_directory
        fname (require-mode "create-directory"
                            (if (null? rest) #o775 (car rest)))))

    (define (create-fifo fname . rest)
      (npl_unix_create_fifo
        fname (require-mode "create-fifo"
                            (if (null? rest) #o664 (car rest)))))

    (define (create-hard-link old-fname new-fname)
      (npl_unix_create_hard_link old-fname new-fname))

    (define (create-symlink old-fname new-fname)
      (npl_unix_create_symlink old-fname new-fname))

    (define (read-symlink fname) (npl_unix_read_symlink fname))
    (define (rename-file old-fname new-fname)
      (npl_unix_rename_file old-fname new-fname))

    (define (set-file-owner fname uid gid)
      (npl_unix_set_file_owner
        fname
        (if (eq? uid owner/unchanged) -1
            (require-id "set-file-owner" uid))
        (if (eq? gid group/unchanged) -1
            (require-id "set-file-owner" gid))))

    ; (seconds . nanoseconds).  The sentinels live in the nanosecond slot,
    ; which a real time-utc keeps in 0..999999999, so every second value
    ; (including -1 and -2) stays an ordinary timestamp (R25).
    (define (time-parts value)
      (cond
        ((eq? value time/now) (cons 0 -1))
        ((eq? value time/unchanged) (cons 0 -2))
        ((time-utc? value)
         (cons (require-time-second "set-file-times" (time-second value))
               (require-nanosecond "set-file-times"
                                   (time-nanosecond value))))
        (else (error "set-file-times: expected a time-utc" value))))

    (define (set-file-times fname . rest)
      (cond
        ((null? rest)
         (set-file-times fname (current-time) (current-time)))
        ((null? (cdr rest))
         (error "set-file-times: needs both times or neither"))
        (else
         (let ((access (time-parts (car rest)))
               (modify (time-parts (car (cdr rest)))))
           (npl_unix_set_file_times fname (car access) (cdr access)
                                    (car modify) (cdr modify))))))

    (define (truncate-file fname/port length)
      (let ((length (require-offset "truncate-file" length)))
        (if (string? fname/port)
            (npl_unix_truncate_file fname/port length)
            (npl_unix_truncate_port fname/port length))))

    (define (set-file-mode fname mode)
      (npl_unix_set_file_mode fname (require-mode "set-file-mode" mode)))
    (define (real-path path) (npl_unix_real_path path))
    (define (file-space path-or-port)
      (if (string? path-or-port)
          (npl_unix_file_space path-or-port)
          (npl_unix_file_space_port path-or-port)))

    ; ---- process state ---------------------------------------------------

    (define (umask) (npl_unix_umask))
    (define (set-umask! mask)
      (npl_unix_set_umask (require-mode "set-umask!" mask)))
    (define (current-directory) (npl_unix_current_directory))
    (define (set-current-directory! directory)
      (npl_unix_set_current_directory directory))
    (define (pid) (npl_unix_pid))
    (define (nice . rest)
      (npl_unix_nice (require-int "nice" (if (null? rest) 1 (car rest)))))
    (define (user-uid) (npl_unix_getuid))
    (define (user-gid) (npl_unix_getgid))
    (define (user-effective-uid) (npl_unix_geteuid))
    (define (user-effective-gid) (npl_unix_getegid))
    (define (user-supplementary-gids) (npl_unix_getgroups))

    ; ---- user and group databases ---------------------------------------

    (define-record-type user-info-record
      (make-user-info-record name uid gid home-dir shell full-name)
      user-info?
      (name user-info:name)
      (uid user-info:uid)
      (gid user-info:gid)
      (home-dir user-info:home-dir)
      (shell user-info:shell)
      (full-name user-info:full-name))

    (define (user-info key)
      (let ((fields (npl_unix_user_info key)))
        (if fields
            (make-user-info-record
              (vector-ref fields 0) (vector-ref fields 1)
              (vector-ref fields 2) (vector-ref fields 3)
              (vector-ref fields 4) (vector-ref fields 5))
            #f)))

    (define (split-commas text)
      (let loop ((start 0) (index 0) (parts '()))
        (cond
          ((= index (string-length text))
           (reverse (cons (substring text start index) parts)))
          ((char=? (string-ref text index) #\,)
           (loop (+ index 1) (+ index 1)
                 (cons (substring text start index) parts)))
          (else (loop start (+ index 1) parts)))))

    (define (capitalize-name name)
      (if (= (string-length name) 0)
          name
          (string-append
            (string (char-upcase (string-ref name 0)))
            (substring name 1 (string-length name)))))

    (define (replace-name-ampersands text name)
      (let loop ((index 0) (out ""))
        (if (= index (string-length text))
            out
            (if (char=? (string-ref text index) #\&)
                (loop (+ index 1) (string-append out name))
                (loop (+ index 1)
                      (string-append out
                                     (string (string-ref text index))))))))

    (define (user-info:parsed-full-name value)
      (if (not (string? (user-info:full-name value)))
          '()
          (let ((parts (split-commas (user-info:full-name value))))
            (if (null? parts)
                parts
                (cons (replace-name-ampersands
                        (car parts) (capitalize-name (user-info:name value)))
                      (cdr parts))))))

    (define-record-type group-info-record
      (make-group-info-record name gid)
      group-info?
      (name group-info:name)
      (gid group-info:gid))

    (define (group-info key)
      (let ((fields (npl_unix_group_info key)))
        (if fields
            (make-group-info-record (vector-ref fields 0)
                                    (vector-ref fields 1))
            #f)))

    ; ---- time and environment -------------------------------------------

    (define (posix-time)
      (let ((fields (npl_unix_time 0)))
        (make-time 'time-utc (vector-ref fields 1)
                   (vector-ref fields 0))))

    (define (monotonic-time)
      (let ((fields (npl_unix_time 1)))
        (make-time 'time-monotonic (vector-ref fields 1)
                   (vector-ref fields 0))))

    (define (set-environment-variable! name value)
      (npl_unix_set_environment name value))

    (define (delete-environment-variable! name)
      (npl_unix_delete_environment name))

    ; ---- temporary files -------------------------------------------------

    (define temp-file-prefix
      (make-parameter
        (string-append
          (if (get-environment-variable "TMPDIR")
              (get-environment-variable "TMPDIR")
              "/tmp")
          "/" (number->string (pid)))))

    (define (create-temp-file . rest)
      (npl_unix_create_temp_file
        (if (null? rest) (temp-file-prefix) (car rest))))

    (define (call-with-temporary-filename maker . rest)
      (let ((prefix (if (null? rest) (temp-file-prefix) (car rest))))
        (let loop ((attempt 0))
          (if (> attempt 100)
              (error "call-with-temporary-filename: no unique name" prefix))
          (let ((name (npl_unix_create_temp_file prefix)))
            (delete-file name)
            (let ((result (maker name)))
              (if (if (pair? result) (car result) result)
                  result
                  (loop (+ attempt 1))))))))))
