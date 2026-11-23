; Static/shared native package acceptance: both artifact forms link, run, and
; the dynamic executable is smaller than the static one.  The check also
; verifies that a narrow import set produces a smaller program than a wider
; one in both modes.
(define-library (tools link-modes)
  (export check-link-modes)
  (import (scheme base)
          (scheme file)
          (scheme write)
          (srfi 13)
          (only (npl internal primitive) read-file)
          (only (npl process) make-temporary-directory run-process*)
          (npl platform)
          (only (npl unix) file-info file-info:size directory-files
                delete-directory))
  (begin
    (define (join directory name)
      (string-append directory "/" name))

    (define (write-text path text)
      (call-with-output-file path
        (lambda (port) (display text port))))

    (define (file-size path)
      (file-info:size (file-info path #t)))

    (define (run-status arguments)
      (run-process* arguments #f "/dev/null" "/dev/null"))

    (define (shared-file? path)
      (if (file-exists? path) #t
          (file-exists? (string-append (substring path 0
                                      (- (string-length path) 6))
                                       ".so"))))

    (define (capture-status arguments output)
      (run-process* arguments #f output #f))

    (define (contains? text needle)
      (if (string-contains text needle) #t #f))

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

    (define (expect ok name condition)
      (if condition
          ok
          (begin
            (display "link-modes: FAIL " (current-error-port))
            (display name (current-error-port))
            (newline (current-error-port))
            #f)))

    (define (remove-tree directory)
      (for-each
        (lambda (name) (delete-file (join directory name)))
        (directory-files directory #t))
      (delete-directory directory))

    (define (check-link-modes root nplc)
      (let* ((directory (make-temporary-directory))
             (small (join directory "small.npl"))
             (wide (join directory "wide.npl"))
             (native (join directory "native.npl"))
             (external (join directory "external.npl"))
             (small-static (join directory "small-static"))
             (small-shared (join directory "small-shared"))
             (wide-static (join directory "wide-static"))
             (wide-shared (join directory "wide-shared"))
             (native-static (join directory "native-static"))
             (native-shared (join directory "native-shared"))
             (external-static (join directory "external-static"))
             (external-shared (join directory "external-shared"))
             (ok #t))
        (write-text small
          "(import (scheme base) (scheme write))\n(display 1)\n")
        (write-text wide
          (string-append
            "(import (scheme base) (scheme write) (srfi 1) (srfi 13)\n"
            "        (npl time) (npl unix))\n"
            "(display (length (string->list \"abc\")))\n"))
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
              (shared-deps (dependencies native-shared)))
          (set! ok (expect ok "static executable has no npl shared dependency"
                           (not (contains? static-deps "libnpl_"))))
          (set! ok (expect ok "dynamic executable uses the shared runtime"
                           (contains? shared-deps "libnpl_runtime")))
          (set! ok (expect ok "dynamic executable uses npl time"
                           (contains? shared-deps "libnpl_time"))))

        (if (if (file-exists? (join root "build/lib/npl/native/libnpl_demo_crc32.a"))
                (shared-file?
                  (join root "build/lib/npl/native/libnpl_demo_crc32.dylib"))
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
               (program (join delivery "bin/program")))
          (set! ok (expect ok "dynamic delivery layout"
                           (if (= (run-status
                                    (list "mkdir" "-p"
                                          (join delivery "bin")
                                          (join delivery "lib")))
                                  0)
                               (= (run-status
                                    (list "cp" "-RL"
                                          (join root "build/lib/npl")
                                          (join delivery "lib/npl")))
                                  0)
                               #f)))
          (set! ok (expect ok "dynamic delivery compile"
                           (if (file-exists?
                                 (join delivery "lib/npl/libnpl_runtime.a"))
                               (= (run-status
                                    (list nplc "--native=shared"
                                          "--runtime"
                                          (join delivery
                                                "lib/npl/libnpl_runtime.a")
                                          native "-o" program))
                                  0)
                               #f)))
          (set! ok (expect ok "dynamic delivery move"
                           (= (run-status (list "mv" delivery moved)) 0)))
          (set! ok (expect ok "dynamic delivery runs after move"
                           (= (run-status
                                (list (join moved "bin/program")))
                              0)))
          (run-status (list "rm" "-rf" moved)))

        (remove-tree directory)
        ok)))
)
