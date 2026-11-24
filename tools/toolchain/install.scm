    (define (clean-command root work . arguments)
      (let ((home (if (get-environment-variable "HOME")
                      (get-environment-variable "HOME")
                      "/tmp")))
        (append (list "/usr/bin/env" "-i" "PATH=/usr/bin:/bin"
                      (string-append "HOME=" home)
                      (string-append "TMPDIR=" work))
                arguments)))

    (define (check-install root)
      (let* ((build (string-append root "/build"))
             (work (make-temporary-directory))
             (prefix (path-join work "prefix with space"))
             (project (path-join work "project"))
             (source (string-append root "/src/compiler/tests/driver-hello.npl"))
             (extension (string-append root "/src/compiler/tests/c-extension"))
             (version (trim-trailing-newline (read-text-file (string-append root "/VERSION"))))
             (nplc (path-join prefix "bin/nplc"))
             (npli (path-join prefix "bin/npli"))
             (runtime (path-join prefix "lib/npl/libnpl_runtime.a")))
        (create-directory project)
        (if (not (= (run-command "cmake" "--install" build "--prefix" prefix) 0))
            (driver-fail "install")
            (let ((required
                    (list (path-join prefix "bin/nplc")
                          (path-join prefix "bin/npli")
                          (path-join prefix "libexec/npl/qbe")
                          (path-join prefix "lib/npl/libnpl_runtime.a")
                          (path-join prefix "lib/npl/native/native.meta")
                          (path-join prefix "lib/npl/native/c-struct-layouts.meta")
                          (path-join prefix "share/npl/lib/scheme/base.sld")
                          (path-join prefix "share/npl/lib/npl/internal/frontend.sld")
                          (path-join prefix "share/npl/src/compiler/main.npl")
                          (path-join prefix "include/npl/npl.h"))))
              (if (not (let loop ((rest required))
                         (if (null? rest) #t
                             (if (file-exists? (car rest))
                                 (loop (cdr rest))
                                 #f))))
                  (driver-fail "install files")
                  (if (file-exists? (path-join prefix
                                               "share/npl/src/compiler/tests"))
                      (driver-fail "tests installed")
                      (let* ((hello (path-join project "hello.npl"))
                             (script (path-join project "script.scm")))
                        (write-text hello (read-text-file source))
                        (write-text script
                          (string-append
                            "(import (scheme base) (scheme write) (scheme char)"
                            " (scheme process-context))\n"
                            "(write (list (char-upcase #\\a) (cdr (command-line))))\n"
                            "(newline)\n"))
                        (with-directory project
                          (lambda ()
                            (if (not (= (run-command (clean-command root work
                                                       nplc "hello.npl")) 0))
                                (driver-fail "installed nplc")
                                (let ((hello-out (path-join work "hello.out")))
                                  (run-process* (list (path-join project "hello"))
                                                   #f hello-out
                                                   (path-join work "hello.err"))
                                  (if (not (string=? (read-text-file hello-out)
                                                     "hello from nplc\n"))
                                      (driver-fail "installed hello output")
                                      (if (not (= (run-command
                                                   (clean-command root work nplc
                                                                  "-c" "hello.npl"))
                                                  0))
                                          (driver-fail "installed -c")
                                          (if (not (= (run-command
                                                       (clean-command root work "cc"
                                                                      "-o" "linked"
                                                                      "hello.o" runtime
                                                                      "-lm"))
                                                      0))
                                              (driver-fail "installed link")
                                              (if (not (= (run-command
                                                           (clean-command root work "cc"
                                                                          "-std=c99" "-Wall"
                                                                          "-Werror" "-I"
                                                                          (path-join prefix
                                                                                     "include/npl")
                                                                          "-c" "-o" "clamp.o"
                                                                          (path-join extension "clamp.c")))
                                                          0))
                                                  (driver-fail "installed header")
                                                  (if (not (= (run-command
                                                               (clean-command root work "cc"
                                                                              "-std=c99" "-c"
                                                                              "-o" "typed.o"
                                                                              (path-join extension "typed.c")))
                                                              0))
                                                      (driver-fail "typed object")
                                                      (if (not (= (run-command
                                                                   (clean-command root work "ar"
                                                                                  "rcs" "libdemotyped.a"
                                                                                  "typed.o"))
                                                                  0))
                                                          (driver-fail "ar")
                                                          (begin
                                                            (write-text
                                                              (path-join project "typed.npl")
                                                              (read-text-file
                                                                (path-join extension "typed.npl")))
                                                            (if (not (= (run-command
                                                                         (clean-command root work nplc
                                                                                        "typed.npl" "-L" "."
                                                                                        "-l" "demotyped"))
                                                                        0))
                                                                (driver-fail "typed link")
                                                                (let ((typed-out (path-join work "typed.out")))
                                                                  (run-process*
                                                                    (list (path-join project "typed"))
                                                                    #f typed-out
                                                                    (path-join work "typed.err"))
                                                                  (if (not (file-equal?
                                                                            (path-join extension "typed.out")
                                                                            typed-out))
                                                                      (driver-fail "typed output")
                                                                      (let ((version-out
                                                                              (path-join work "v1")))
                                                                        (run-process*
                                                                          (clean-command root work nplc "--version")
                                                                          #f version-out
                                                                          (path-join work "v1.err"))
                                                                        (if (not (string=?
                                                                                   (trim-trailing-newline (read-text-file version-out))
                                                                                   (string-append "nplc " version)))
                                                                            (driver-fail "installed version")
                                                                            (let ((script-out
                                                                                    (path-join work "script.out")))
                                                                              (run-process*
                                                                                (clean-command root work npli
                                                                                               "script.scm" "x")
                                                                                #f script-out
                                                                                (path-join work "script.err"))
                                                                              (if (not (string=?
                                                                                         (trim-trailing-newline (read-text-file script-out))
                                                                                         "(#\\A (\"x\"))"))
                                                                                  (driver-fail "installed npli script")
                                                                                  (let ((prompt (path-join work "prompt.in"))
                                                                                        (prompt-out (path-join work "prompt.out")))
                                                                                    (write-text prompt "(+ 40 2)\n")
                                                                                    (run-process*
                                                                                      (clean-command root work npli "--no-prompt")
                                                                                      prompt prompt-out
                                                                                      (path-join work "prompt.err"))
                                                                                    (if (not (string=?
                                                                                               (trim-trailing-newline (read-text-file prompt-out))
                                                                                               "42"))
                                                                                        (driver-fail "installed prompt")
                                                                                        (let ((installed-ssa
                                                                                                (path-join work "installed.ssa"))
                                                                                              (tree-ssa
                                                                                                (path-join work "tree.ssa")))
                                                                                          (run-process*
                                                                                            (clean-command root work nplc
                                                                                                           "--emit=ssa" "hello.npl")
                                                                                            #f installed-ssa
                                                                                            (path-join work "ssa.err"))
                                                                                          (run-process*
                                                                                            (list (path-join root "build/bin/nplc")
                                                                                                  "--emit=ssa" "hello.npl")
                                                                                            #f tree-ssa
                                                                                            (path-join work "ssa2.err"))
                                                                                          (if (file-equal? installed-ssa tree-ssa)
                                                                                              (begin
                                                                                                (display "install layout: PASS")
                                                                                                (newline)
                                                                                                #t)
                                                                                              (driver-fail "installed SSA"))))))))))))))))))))))))))))
)
)

    (define (empty-directory? directory)
      (null? (directory-files directory #t)))

    (define (string-contains-index text needle)
      (let loop ((index 0))
        (cond
          ((> index (- (string-length text) (string-length needle))) #f)
          ((string=? (substring text index (+ index (string-length needle)))
                     needle) index)
          (else (loop (+ index 1))))))

    (define (line-after-prefix text prefix)
      (let ((index (string-contains-index text prefix)))
        (if (not index)
            #f
            (let ((start (+ index (string-length prefix)))
                  (end (let loop ((cursor (+ index (string-length prefix))))
                         (if (= cursor (string-length text))
                             cursor
                             (if (char=? (string-ref text cursor) #\newline)
                                 cursor
                                 (loop (+ cursor 1)))))))
              (substring text start end)))))
