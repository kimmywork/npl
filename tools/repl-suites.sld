; REPL and script acceptance in NPL.  This replaces tools/check_repl.sh:
; the checks are the same, but orchestration, file comparison, and failure
; reporting are NPL.  External commands are limited to the toolchain pieces
; the test exercises (nplc, npli, the host C compiler, cp/rm for fixtures).
(define-library (tools repl-suites)
  (export check-repl-suites)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (scheme write)
          (only (npl internal primitive) read-file)
          (only (npl process) make-temporary-directory run-process*)
          (npl platform)
          (npl unix)
          (only (npl string) string-contains)
          (only (npl util) path-join)
          (only (npl test util) write-text file-size file-empty?
                file-equal? expect remove-tree run-ok?
                with-directory with-environment trim-newline))
  (begin
    (define FAILURES '())

    (define (fail message)
      (set! FAILURES (cons message FAILURES))
      #f)

    (define (find-text text needle start index)
      (if (> (+ index (string-length needle)) (string-length text))
          #f
          (if (string=? (substring text index
                                   (+ index (string-length needle)))
                        needle)
              index
              (find-text text needle start (+ index 1)))))

    (define (replace-all text from to)
      (if (= (string-length from) 0)
          text
          (let loop ((start 0) (out ""))
            (let ((index (if (>= start (string-length text))
                             #f
                             (find-text text from start start))))
              (if (not index)
                  (string-append out (substring text start
                                                (string-length text)))
                  (loop (+ index (string-length from))
                        (string-append out
                                       (substring text start index)
                                       to)))))))

    (define (replace-lines path replacements output)
      (let ((text (read-file path)))
        (let loop ((rest replacements) (text text))
          (if (null? rest)
              (write-text output text)
              (loop (cdr rest)
                    (replace-all text (car (car rest)) (cdr (car rest))))))))

    (define (copy-file source output)
      (write-text output (read-file source)))

    (define (run-repl root npli work cwd input expected-out expected-err label)
      (let ((out (path-join work (string-append label ".out")))
            (err (path-join work (string-append label ".err"))))
        (let ((status (with-directory
                        cwd
                        (lambda ()
                          (run-process* (list npli "--no-prompt") input out err)))))
          (if (not (= status 0))
              (expect #f label #f)
              (let ((ok (file-equal? out expected-out)))
                (if ok
                    (expect ok label (file-equal? err expected-err))
                    (expect ok label #f)))))))

    (define (absolute-path path)
      (if (if (> (string-length path) 0)
              (char=? (string-ref path 0) #\/)
              #f)
          path
          (string-append (current-directory) "/" path)))

    (define (script-surface npli native script surface out)
      (with-environment
        "NPLI_SCRIPT_CHECK" "on"
        (lambda ()
          (cond
            ((string=? surface "npli")
             (= (run-process* (list npli (path-join script "program.scm")
                          "7" "two words")
                     #f out (string-append out ".err")) 7))
            ((string=? surface "native")
             (= (run-process* (list native "7" "two words") #f out
                     (string-append out ".err")) 7))
            (else
             (= (run-process* (list "guile" "--r7rs" "-q" "--no-auto-compile"
                          "-s" (path-join script "program.scm")
                          "7" "two words")
                     #f out (string-append out ".err")) 7))))))

    (define (check-repl-suites root nplc npli)
      (let ((nplc (absolute-path nplc))
            (npli (absolute-path npli))
            (work (make-temporary-directory))
            (tests (path-join root "src/compiler/tests"))
            (extension (path-join root "src/compiler/tests/c-extension"))
            (repl-tests (path-join root "src/repl/tests"))
            (ok #t))
        (set! FAILURES '())
        (let ((version (trim-newline
                         (let ((out (path-join work "version.out")))
                           (run-process* (list npli "--version") #f out #f)
                           (read-file out)))))
          (set! ok (expect ok "npli version"
                           (string=? version
                                     (string-append
                                       "npli "
                                       (trim-newline
                                         (read-file (path-join root "VERSION"))))))))
        (set! ok (expect ok "evaluator core compile"
                         (run-ok? (list nplc (path-join root "src/evaluator/core-tests.npl")
                                        "-o" (path-join work "evaluator-core"))
                                  #f #f #f)))
        (let ((out (path-join work "evaluator.out")))
          (set! ok (expect ok "evaluator core run"
                           (if (run-ok? (list (path-join work "evaluator-core"))
                                        #f out #f)
                               (string-contains (read-file out)
                                          "PASS evaluator-core 42")
                               #f))))
        (set! ok (expect ok "differential native compile"
                         (run-ok? (list nplc
                                        (path-join root "src/repl/differential-native.npl")
                                        "-o" (path-join work "differential-native"))
                                  #f #f #f)))
        (set! ok (expect ok "differential evaluator compile"
                         (run-ok? (list nplc
                                        (path-join root "src/repl/differential-evaluator.npl")
                                        "-o" (path-join work "differential-evaluator"))
                                  #f #f #f)))
        (let ((native-out (path-join work "differential-native.out"))
              (native-err (path-join work "differential-native.err"))
              (eval-out (path-join work "differential-evaluator.out"))
              (eval-err (path-join work "differential-evaluator.err")))
          (set! ok (expect ok "differential native run"
                           (run-ok? (list (path-join work "differential-native"))
                                    #f native-out native-err)))
          (set! ok (expect ok "differential evaluator run"
                           (run-ok? (list (path-join work "differential-evaluator")
                                          (path-join root
                                                "src/repl/tests/differential-fixture.scm"))
                                    #f eval-out eval-err)))
          (set! ok (expect ok "differential stdout"
                           (file-equal? native-out eval-out)))
          (set! ok (expect ok "differential stderr"
                           (if (file-empty? native-err)
                               (file-empty? eval-err)
                               #f))))

        (for-each
          (lambda (name)
            (let ((label (car name))
                  (input (path-join repl-tests (car (cdr name)))))
              (let ((cwd (if (if (string=? label "libraries")
                                 #t
                                 (string=? label "load"))
                             repl-tests
                             work)))
                (set! ok (expect ok label
                                 (run-repl root npli work cwd input
                                           (path-join repl-tests
                                                 (string-append label ".out"))
                                           (path-join repl-tests
                                                 (string-append label ".err"))
                                           label))))))
          '(("persistent" "persistent.in")
            ("libraries" "libraries.in")
            ("primitive-errors" "primitive-errors.in")
            ("eval" "eval.in")
            ("interop" "interop.in")
            ("load" "load.in")))

        (let ((input (path-join work "prompt.in"))
              (expected (path-join work "prompt.expected"))
              (out (path-join work "prompt.out"))
              (err (path-join work "prompt.err")))
          (write-text input "(+ 40 2)\n")
          (write-text expected "> 42\n> ")
          (set! ok (expect ok "prompt output"
                           (if (run-ok? (list npli) input out err)
                               (file-equal? expected out)
                               #f)))
          (set! ok (expect ok "prompt stderr" (file-empty? err))))

        (let ((script (path-join root "src/repl/tests/script"))
              (native (path-join work "script-native")))
          (set! ok (expect ok "script native compile"
                           (run-ok? (list nplc (path-join script "native.npl")
                                          "-o" native) #f #f #f)))
          (for-each
            (lambda (surface)
              (let ((out (path-join work (string-append surface ".out"))))
                (set! ok (expect ok (string-append "script " surface)
                                 (script-surface npli native script surface out)))))
            '("npli" "native" "guile"))
          (set! ok (expect ok "script npli/native output"
                           (file-equal? (path-join work "npli.out")
                                        (path-join work "native.out"))))
          (set! ok (expect ok "script npli/guile output"
                           (file-equal? (path-join work "npli.out")
                                        (path-join work "guile.out"))))
          (set! ok (expect ok "script arguments"
                           (string-contains (read-file (path-join work "npli.out"))
                                      "(arguments (\"7\" \"two words\"))"))))

        (let ((source (path-join work "shebang.scm"))
              (out (path-join work "shebang.out")))
          (write-text source
            (string-append
              "#!/usr/bin/env npli\n"
              "(import (scheme base) (scheme write))\n"
              "(display 42)\n"))
          (set! ok (expect ok "shebang"
                           (if (run-ok? (list npli source)
                                        #f out (path-join work "shebang.err"))
                               (string=? (read-file out) "42")
                               #f)))
          (let ((err (path-join work "shebang-nplc.err")))
            (set! ok (expect ok "nplc shebang diagnostic"
                             (if (not (run-ok?
                                        (list nplc source "-o"
                                              (path-join work "shebang-native"))
                                        #f #f err))
                                 (if (string-contains (read-file err) "line 1")
                                     (string-contains (read-file err) "shebang")
                                     #f)
                                 #f)))))

        (let ((bad (path-join work "bad.scm"))
              (err (path-join work "bad.err")))
          (write-text bad "(import (scheme base))\n(car 1)\n")
          (set! ok (expect ok "runtime error status"
                           (= (run-process* (list npli bad) #f #f err) 70)))
          (set! ok (expect ok "runtime error message"
                           (string-contains (read-file err)
                                      "error: car: expected a pair"))))
        (set! ok (expect ok "missing script status"
                         (= (run-process* (list npli (path-join work "missing.scm"))
                                 #f #f (path-join work "missing.err"))
                            66)))
        (set! ok (expect ok "directory script status"
                         (= (run-process* (list npli work) #f #f
                                 (path-join work "directory-script.err"))
                            70)))
        (set! ok (expect ok "directory script diagnostic"
                         (string-contains (read-file (path-join work "directory-script.err"))
                                    "error: ")))
        (set! ok (expect ok "bad option status"
                         (= (run-process* (list npli "--bogus") #f #f
                                 (path-join work "option.err")) 2)))

        (let* ((darwin? (platform-feature? 'darwin))
               (suffix (if darwin? "dylib" "so"))
               (shared (if darwin?
                           (list "-dynamiclib" "-undefined" "dynamic_lookup")
                           (list "-shared" "-fPIC")))
               (cc (if (get-environment-variable "CC")
                       (get-environment-variable "CC")
                       "cc"))
               (clamp (path-join work (string-append "libdemoclamp." suffix)))
               (pointers (path-join work (string-append "libdemopointers." suffix)))
               (values (path-join work (string-append "libdemovalues." suffix)))
               (typed (path-join work (string-append "libdemotyped." suffix))))
          (set! ok (expect ok "clamp shared library"
                           (run-ok? (append
                                      (list cc "-std=c99" "-Wall" "-Werror"
                                            "-I" (path-join root "runtime"))
                                      (append shared
                                              (list "-o" clamp
                                                    (path-join extension "clamp.c"))))
                                    #f #f #f)))
          (copy-file (path-join extension "main.npl")
                     (path-join work "c-extension.scm"))
          (set! ok (expect ok "pointers shared library"
                           (run-ok? (append
                                      (list cc "-std=c99" "-Wall" "-Werror"
                                            "-I" (path-join root "runtime"))
                                      (append shared
                                              (list "-o" pointers
                                                    (path-join extension "pointers.c"))))
                                    #f #f #f)))
          (set! ok (expect ok "pointers under npli"
                           (if (run-ok? (list npli "-l" pointers
                                              (path-join extension "pointers.npl"))
                                        #f (path-join work "pointers.out") #f)
                               (file-equal? (path-join extension "pointers.out")
                                            (path-join work "pointers.out"))
                               #f)))
          (set! ok (expect ok "values shared library"
                           (run-ok? (append
                                      (list cc "-std=c99" "-Wall" "-Werror"
                                            "-I" (path-join root "runtime"))
                                      (append shared
                                              (list "-o" values
                                                    (path-join extension "values.c"))))
                                    #f #f #f)))
          (set! ok (expect ok "values under npli"
                           (if (run-ok? (list npli "-l" values
                                              (path-join extension "values.npl"))
                                        #f (path-join work "values.out") #f)
                               (file-equal? (path-join extension "values.out")
                                            (path-join work "values.out"))
                               #f)))
          (set! ok (expect ok "typed stubs"
                           (run-ok? (list nplc "--emit=c-stubs"
                                            (path-join extension "typed.npl"))
                                    #f (path-join work "typed-stubs.c") #f)))
          (set! ok (expect ok "typed shared library"
                           (run-ok? (append
                                      (list cc "-std=c99" "-Wall" "-Werror"
                                            "-I" (path-join root "runtime"))
                                      (append shared
                                              (list "-o" typed
                                                    (path-join work "typed-stubs.c")
                                                    (path-join extension "typed.c"))))
                                    #f #f #f)))
          (set! ok (expect ok "typed under npli"
                           (if (run-ok? (list npli "-l" typed
                                              (path-join extension "typed.npl"))
                                        #f (path-join work "typed.out") #f)
                               (file-equal? (path-join extension "typed.out")
                                            (path-join work "typed.out"))
                               #f)))
          (set! ok (expect ok "CExtension under npli"
                           (if (run-ok? (list npli "-l" clamp
                                              (path-join work "c-extension.scm"))
                                        #f (path-join work "c-extension.out") #f)
                               (file-equal? (path-join extension "expected.out")
                                            (path-join work "c-extension.out"))
                               #f)))
          (write-text (path-join work "c-environments.scm")
            (string-append
              "(import (scheme base) (scheme write) (scheme eval))\n"
              "(define-c-primitive (clamp (x fixnum) (lo fixnum) (hi fixnum))"
              " fixnum \"demo_clamp\")\n"
              "(define env (environment '(scheme base)))\n"
              "(write (list (clamp 15 0 10)\n"
              "             (eval '(%c-call \"demo_clamp\" (any any any)"
              " -4 0 3) env)\n"
              "             (eval '((%c-procedure \"demo_clamp\""
              " (any any any)) 9 0 3) env)))\n"))
          (set! ok (expect ok "C environments"
                           (if (run-ok? (list npli "-l" clamp
                                              (path-join work "c-environments.scm"))
                                        #f (path-join work "c-environments.out") #f)
                               (string=? (read-file
                                           (path-join work "c-environments.out"))
                                         "(10 0 3)")
                               #f))))

        (replace-lines (path-join tests "c-primitives.npl")
          (list (cons "(include \"framework.npl\")"
                      "(import (npl test))"))
          (path-join work "c-primitives.scm"))
        (set! ok (expect ok "c-primitives under npli"
                         (with-directory
                           tests
                           (lambda ()
                             (run-ok? (list npli
                                            (path-join work "c-primitives.scm"))
                                      #f (path-join work "c-primitives.out") #f)))))
        (set! ok (expect ok "c-primitives count"
                         (string-contains (read-file (path-join work "c-primitives.out"))
                                    "PASS c-primitives 11")))

        (for-each
          (lambda (contract)
            (let ((name (car contract))
                  (message (cdr contract)))
              (copy-file (path-join tests
                               (string-append "contracts/c-primitive-"
                                              name ".npl"))
                         (path-join work "contract.scm"))
              (let ((status (run-process* (list npli (path-join work "contract.scm"))
                                 #f #f (path-join work "rejected.err"))))
                (set! ok (expect ok (string-append "c-primitive " name)
                                 (if (= status 70)
                                     (string-contains (read-file
                                                  (path-join work "rejected.err"))
                                                (string-append "error: "
                                                               message))
                                     #f))))))
          '(("nested" . "define-c-primitive is valid at top level only")
            ("type" . "unknown C primitive type: pear")
            ("conflict" . "C primitive declaration conflicts with registered npl_car")
            ("symbol" . "invalid C symbol: npl-f")))
        (for-each
          (lambda (contract)
            (let ((name (car contract))
                  (message (cdr contract)))
              (let ((status (run-process* (list npli
                                        (path-join tests
                                              (string-append
                                                "contracts/c-function-"
                                                name ".npl")))
                                 #f #f (path-join work "rejected.err"))))
                (set! ok (expect ok (string-append "c-function " name)
                                 (if (= status 70)
                                     (string-contains (read-file
                                                  (path-join work "rejected.err"))
                                                (string-append "error: "
                                                               message))
                                     #f))))))
          '(("nested" . "define-c-function is valid at top level only")
            ("type" . "unknown C function type: float")
            ("void-argument" . "void is valid only as a define-c-function result")
            ("pointer-out-result" . "pointer-out is valid only as a define-c-function argument")
            ("arrow" . "malformed define-c-function")))
        (let ((status (run-process* (list npli (path-join extension "typed.npl"))
                           #f #f (path-join work "rejected.err"))))
          (set! ok (expect ok "typed stub missing"
                           (if (= status 70)
                               (string-contains (read-file (path-join work "rejected.err"))
                                          "error: C stub not found: npl_cfn_demo_add")
                               #f))))
        (let ((status (run-process* (list npli (path-join work "c-extension.scm"))
                           #f #f (path-join work "rejected.err"))))
          (set! ok (expect ok "C function missing"
                           (if (= status 70)
                               (string-contains (read-file (path-join work "rejected.err"))
                                          "error: C function not found: demo_clamp")
                               #f))))
        (let ((status (run-process* (list npli "-l" (path-join work "missing-library")
                                 (path-join work "c-extension.scm"))
                           #f #f (path-join work "rejected.err"))))
          (set! ok (expect ok "missing npli library"
                           (if (= status 66)
                               (string-contains (read-file (path-join work "rejected.err"))
                                          (string-append
                                            "npli: cannot load "
                                            (path-join work "missing-library")))
                               #f))))

        (replace-lines (path-join tests "primitive-errors.npl")
          (list (cons "(include \"framework.npl\")"
                      "(import (npl test))"))
          (path-join work "primitive-errors.scm"))
        (set! ok (expect ok "primitive-errors under npli"
                         (if (run-ok? (list npli
                                             (path-join work "primitive-errors.scm"))
                                      #f (path-join work "primitive-errors-suite.out")
                                      #f)
                             (string-contains (read-file
                                          (path-join work "primitive-errors-suite.out"))
                                        "PASS primitive-errors 37")
                             #f)))

        (let ((edited (path-join work "stdlib-edited")))
          (set! ok (expect ok "copy stdlib"
                           (run-ok? (list "cp" "-R" (path-join root "lib") edited)
                                    #f #f #f)))
          (replace-lines (path-join root "lib/scheme/time.sld")
            (list (cons "(define (current-jiffy) (npl_current_jiffy))"
                        "(define (current-jiffy) 42)"))
            (path-join edited "scheme/time.sld"))
          (set! ok (expect ok "edited library differs"
                           (not (file-equal? (path-join root "lib/scheme/time.sld")
                                             (path-join edited "scheme/time.sld")))))
          (write-text (path-join work "edited-library.scm")
            (string-append
              "(import (scheme base) (scheme write) (scheme time))\n"
              "(write (list (current-jiffy) (+ 20 22)))\n"))
          (set! ok (expect ok "edited library run-process*"
                           (if (run-ok? (list npli "--stdlib-root" edited
                                              (path-join work "edited-library.scm"))
                                        #f (path-join work "edited-library.out") #f)
                               (string=? (read-file
                                           (path-join work "edited-library.out"))
                                         "(42 42)")
                               #f)))
          (set! ok (expect ok "native library run-process*"
                           (if (run-ok? (list npli
                                              (path-join work "edited-library.scm"))
                                        #f (path-join work "native-library.out") #f)
                               (not (string=? (read-file
                                                (path-join work "native-library.out"))
                                              "(42 42)"))
                               #f))))

        (replace-lines (path-join tests "runtime-errors.npl")
          (list (cons "(include \"framework.npl\")"
                      "(import (npl test))"))
          (path-join work "runtime-errors.scm"))
        (set! ok (expect ok "runtime-errors under npli"
                         (if (run-ok? (list npli
                                             (path-join work "runtime-errors.scm"))
                                      #f (path-join work "runtime-errors-suite.out")
                                      #f)
                             (string-contains (read-file
                                          (path-join work "runtime-errors-suite.out"))
                                        "PASS runtime-errors 14")
                             #f)))
        (set! ok (expect ok "npli -e"
                         (if (run-ok? (list npli "-e"
                                            "(import (scheme base) (scheme write))"
                                            "-e" "(write (+ 40 2))")
                                      #f (path-join work "expr.out") #f)
                             (string=? (read-file (path-join work "expr.out")) "42")
                             #f)))
        (write-text (path-join work "no-cc.scm")
          (string-append
            "(import (scheme base) (scheme write) (npl time) (npl unix))\n"
            "(write (list (time? (current-time))"
            " (time-monotonic? (monotonic-time))))\n"))
        (set! ok (expect ok "npli without cc"
                         (with-environment
                           "NPL_CC" "/usr/bin/false"
                           (lambda ()
                             (if (run-ok? (list npli (path-join work "no-cc.scm"))
                                          #f (path-join work "no-cc.out") #f)
                                 (string=? (read-file (path-join work "no-cc.out"))
                                           "(#t #t)")
                                 #f)))))
        (remove-tree work)
        (if (null? FAILURES)
            (begin
              (display "npli REPL and script mode: PASS")
              (newline)
              #t)
            (begin
              (for-each
                (lambda (message)
                  (display "repl-suites: FAILED " (current-error-port))
                  (display message (current-error-port))
                  (newline (current-error-port)))
                (reverse FAILURES))
              #f)))))
)
