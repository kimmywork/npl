; Static/shared native package acceptance: both artifact forms link, run, and
; the dynamic executable is smaller than the static one.  The check also
; verifies that a narrow import set produces a smaller program than a wider
; one in both modes.
(define-library (tools link-modes)
  (export check-link-modes)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (scheme write)
          (only (npl internal primitive) read-file)
          (only (npl process) make-temporary-directory run-process*)
          (npl platform)
          (only (npl string) string-contains)
          (only (npl util) path-join)
          (only (npl test util) write-text file-size expect remove-tree
                run-status with-environment))
  (begin
    (define (shared-file? path)
      (if (file-exists? path) #t
          (file-exists? (string-append (substring path 0
                                      (- (string-length path) 6))
                                       ".so"))))

    (define (capture-status arguments output)
      (run-process* arguments #f output #f))

    (define (dependencies path)
      (let ((output (string-append path ".deps")))
        (capture-status
          (if (platform-feature? 'darwin)
              (list "otool" "-L" path)
              (list "ldd" path))
          output)
        (let ((text (if (file-exists? output) (read-file output) "")))
          text)))

    (define (compile nplc mode source output)
      (= (run-status
           (list nplc (string-append "--native=" mode) source "-o" output))
         0))

    ; Compile without --native so NPL_NATIVE_LINK selects the form.
    (define (compile-default nplc source output)
      (= (run-status (list nplc source "-o" output)) 0))

    (define (with-native-environment value thunk)
      (with-environment "NPL_NATIVE_LINK" value thunk))

    (define (check-link-modes root nplc)
      (let* ((directory (make-temporary-directory))
             (small (path-join directory "small.npl"))
             (wide (path-join directory "wide.npl"))
             (native (path-join directory "native.npl"))
             (external (path-join directory "external.npl"))
             (small-static (path-join directory "small-static"))
             (small-shared (path-join directory "small-shared"))
             (env-shared (path-join directory "env-shared"))
             (env-override (path-join directory "env-override"))
             (env-invalid (path-join directory "env-invalid"))
             (wide-static (path-join directory "wide-static"))
             (wide-shared (path-join directory "wide-shared"))
             (native-static (path-join directory "native-static"))
             (native-shared (path-join directory "native-shared"))
             (external-static (path-join directory "external-static"))
             (external-shared (path-join directory "external-shared"))
             (ok #t))
        (write-text small
          "(import (scheme base) (scheme write))\n(display 1)\n")
        (write-text wide
          (string-append
            "(import (scheme base) (scheme write) (srfi 1) (srfi 13)\n"
            "        (srfi 230) (npl time) (npl unix))\n"
            "(display (length (string->list \"abc\")))\n"
            "(display (atomic-box-ref (make-atomic-box 'shared)))\n"))
        (write-text native
          (string-append
            "(import (scheme base) (scheme write) (npl time))\n"
            "(display (time-second (current-time)))\n"))
        (write-text external
          (string-append
            "(import (scheme base) (scheme write) (npl demo crc32))\n"
            "(display (crc32 (string->utf8 \"123456789\")))\n"))

        (set! ok (expect ok "small static compile"
                         (compile nplc "static" small small-static)))
        (set! ok (expect ok "small shared compile"
                         (compile nplc "shared" small small-shared)))
        (set! ok (expect ok "NPL_NATIVE_LINK=shared compile"
                         (with-native-environment
                           "shared"
                           (lambda ()
                             (compile-default nplc small env-shared)))))
        (set! ok (expect ok "unknown NPL_NATIVE_LINK is rejected"
                         (with-native-environment
                           "dynamic"
                           (lambda ()
                             (not (compile-default nplc small env-invalid))))))
        (set! ok (expect ok "--native overrides NPL_NATIVE_LINK"
                         (with-native-environment
                           "static"
                           (lambda ()
                             (compile nplc "shared" small env-override)))))
        ; R19: NPL_C_STRUCT_LAYOUTS is searched before the core cache, not
        ; instead of it.  A package file that only knows iovec must still
        ; let (npl time) find timespec/tm beside the installed nplc, with
        ; no C compiler available (NPL_CC=false).  NPL_CC is also the
        ; linker, so stop at QBE: the layout lookup is all this checks.
        (let ((package (path-join directory "package-layouts.meta"))
              (cached (path-join directory "cache-fallback.ssa")))
          (write-text package
            (string-append
              "(c-struct-layouts\n"
              "  (\"struct iovec\" (size . 16) (iov_base . 0) (iov_len . 8))\n"
              ")\n"))
          (set! ok (expect ok "package layout cache falls back to the core cache"
                           (with-environment "NPL_C_STRUCT_LAYOUTS" package
                             (lambda ()
                               (with-environment "NPL_CC" "/usr/bin/false"
                                 (lambda ()
                                   (= (run-process*
                                        (list nplc "--emit-qbe" native)
                                        #f cached "/dev/null")
                                      0))))))))
        (set! ok (expect ok "wide static compile"
                         (compile nplc "static" wide wide-static)))
        (set! ok (expect ok "wide shared compile"
                         (compile nplc "shared" wide wide-shared)))
        (set! ok (expect ok "native static compile"
                         (compile nplc "static" native native-static)))
        (set! ok (expect ok "native shared compile"
                         (compile nplc "shared" native native-shared)))

        (set! ok (expect ok "small static runs" (= (run-status (list small-static)) 0)))
        (set! ok (expect ok "small shared runs" (= (run-status (list small-shared)) 0)))
        (set! ok (expect ok "wide static runs" (= (run-status (list wide-static)) 0)))
        (set! ok (expect ok "wide shared runs" (= (run-status (list wide-shared)) 0)))
        (set! ok (expect ok "native static runs" (= (run-status (list native-static)) 0)))
        (set! ok (expect ok "native shared runs" (= (run-status (list native-shared)) 0)))

        (set! ok (expect ok "dynamic small is smaller"
                         (< (file-size small-shared) (file-size small-static))))
        (set! ok (expect ok "dynamic wide is smaller"
                         (< (file-size wide-shared) (file-size wide-static))))
        (set! ok (expect ok "dynamic native is smaller"
                         (< (file-size native-shared) (file-size native-static))))
        (set! ok (expect ok "narrow static is smaller"
                         (< (file-size small-static) (file-size wide-static))))
        (set! ok (expect ok "narrow dynamic is smaller"
                         (< (file-size small-shared) (file-size wide-shared))))

        (let ((static-deps (dependencies small-static))
              (shared-deps (dependencies native-shared))
              (env-deps (dependencies env-shared))
              (override-deps (dependencies env-override)))
          (set! ok (expect ok "static executable has no npl shared dependency"
                           (not (string-contains static-deps "libnpl_"))))
          (set! ok (expect ok "dynamic executable uses the shared runtime"
                           (string-contains shared-deps "libnpl_runtime")))
          (set! ok (expect ok "dynamic executable uses npl time"
                           (string-contains shared-deps "libnpl_time")))
          (set! ok (expect ok "environment dynamic executable uses shared runtime"
                           (string-contains env-deps "libnpl_runtime")))
          (set! ok (expect ok "--native overrides the environment"
                           (string-contains override-deps "libnpl_runtime"))))

        (if (if (file-exists? (path-join root "build/lib/npl/native/libnpl_demo_crc32.a"))
                (shared-file?
                  (path-join root "build/lib/npl/native/libnpl_demo_crc32.dylib"))
                #f)
            (begin
              (set! ok (expect ok "external static compile"
                               (compile nplc "static" external external-static)))
              (set! ok (expect ok "external shared compile"
                               (compile nplc "shared" external external-shared)))
              (set! ok (expect ok "external static runs"
                               (= (run-status (list external-static)) 0)))
              (set! ok (expect ok "external shared runs"
                               (= (run-status (list external-shared)) 0)))
              (set! ok (expect ok "dynamic external is smaller"
                               (< (file-size external-shared)
                                  (file-size external-static)))))
            #f)

        ; A dynamic delivery is a bin/ + lib/npl tree; it must still run
        ; after the whole prefix moves.
        (let* ((delivery (make-temporary-directory))
               (moved (string-append delivery "-moved"))
               (program (path-join delivery "bin/program")))
          (set! ok (expect ok "dynamic delivery layout"
                           (if (= (run-status
                                    (list "mkdir" "-p"
                                          (path-join delivery "bin")
                                          (path-join delivery "lib")))
                                  0)
                               (= (run-status
                                    (list "cp" "-RL"
                                          (path-join root "build/lib/npl")
                                          (path-join delivery "lib/npl")))
                                  0)
                               #f)))
          (set! ok (expect ok "dynamic delivery compile"
                           (if (file-exists?
                                 (path-join delivery "lib/npl/libnpl_runtime.a"))
                               (= (run-status
                                    (list nplc "--native=shared"
                                          "--runtime"
                                          (path-join delivery
                                                "lib/npl/libnpl_runtime.a")
                                          native "-o" program))
                                  0)
                               #f)))
          (set! ok (expect ok "dynamic delivery move"
                           (= (run-status (list "mv" delivery moved)) 0)))
          (set! ok (expect ok "dynamic delivery runs after move"
                           (= (run-status
                                (list (path-join moved "bin/program")))
                              0)))
          (run-status (list "rm" "-rf" moved)))

        (remove-tree directory)
        ok)))
)
