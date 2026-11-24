; Toolchain checks expressed in NPL.  Each function returns #t/#f and prints
; its own evidence; tools/run-tests.npl treats them like any other suite.
(define-library (tools toolchain)
  (export check-gc check-current-fixed-point
          check-numeric-reference check-core-consistency
          check-install check-driver)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (scheme write)
          (only (npl string) string-contains)
          (only (npl util) path-join)
          (npl process)
          (npl unix)
          (only (npl test util) read-text-file write-text text-lines
                file-equal? with-directory)
          (only (npl test expect) run-fixture-directory))
    (include "toolchain/install.scm")
  (begin
    (define (line-count path)
      (let ((text (read-text-file path)))
        (let loop ((index 0) (count 0))
          (if (= index (string-length text))
              count
              (if (char=? (string-ref text index) #\newline)
                  (loop (+ index 1) (+ count 1))
                  (loop (+ index 1) count))))))

    (define (run-command . arguments)
      (run-process*
        (if (if (pair? arguments) (pair? (car arguments)) #f)
            (car arguments)
            arguments)
        #f #f #f))

    (define (cleanup-file path)
      (if (file-exists? path) (delete-file path) #f))

    (define (trim-trailing-newline text)
      (if (= (string-length text) 0)
          text
          (if (char=? (string-ref text (- (string-length text) 1)) #\newline)
              (substring text 0 (- (string-length text) 1))
              text)))

    (define (check-gc root)
      (let ((build (string-append root "/build"))
            (work (make-temporary-directory))
            (nursery (get-environment-variable "NPL_GC_NURSERY_BYTES"))
            (verify (get-environment-variable "NPL_GC_VERIFY")))
        (let ((configured (= (run-command "cmake" "-S" root "-B" build
                                          "-G" "Ninja") 0))
              (built #f))
          (if configured
              (set! built (= (run-command "ninja" "-C" build "gc_stress") 0))
              #f)
          (if (not built)
              (begin
                (display "gc_stress build failed")
                (newline)
                #f)
              (let* ((first-out (path-join work "first.out"))
                     (first-err (path-join work "first.err"))
                     (first-status
                       (run-process* (list (string-append build
                                                             "/runtime/gc_stress"))
                                        #f first-out first-err))
                     (first-text (read-text-file first-out))
                     (first-ok
                       (if (= first-status 0)
                           (string-contains first-text
                                             "PASS: 1000000 allocations")
                           #f)))
                (display first-text)
                (set-environment-variable! "NPL_GC_NURSERY_BYTES" "65536")
                (set-environment-variable! "NPL_GC_VERIFY" "1")
                (let* ((second-out (path-join work "second.out"))
                       (second-err (path-join work "second.err"))
                       (second-status
                         (run-process* (list (string-append build
                                                               "/runtime/gc_stress"))
                                          #f second-out second-err))
                       (second-text (read-text-file second-out))
                       (second-ok
                         (if (= second-status 0)
                             (if (string-contains second-text
                                                   "PASS: 1000000 allocations")
                                 (if (string-contains second-text
                                                       " minor collections")
                                     (if (string-contains
                                           second-text ", 0 minor collections")
                                         #f
                                         #t)
                                     #f)
                                 #f)
                             #f)))
                  (display second-text)
                  (if nursery
                      (set-environment-variable! "NPL_GC_NURSERY_BYTES" nursery)
                      (delete-environment-variable! "NPL_GC_NURSERY_BYTES"))
                  (if verify
                      (set-environment-variable! "NPL_GC_VERIFY" verify)
                      (delete-environment-variable! "NPL_GC_VERIFY"))
                  (cleanup-file first-out)
                  (cleanup-file first-err)
                  (cleanup-file second-out)
                  (cleanup-file second-err)
                  (delete-directory work)
                  (if (if first-ok second-ok #f)
                      (begin
                        (display "runtime GC: PASS")
                        (newline)
                        #t)
                      #f)))))))

    (define (build-compiler qbe cc runtime input binary work stem)
      (let ((assembly (string-append work "/" stem ".s")))
        (if (not (= (run-command qbe "-o" assembly input) 0))
            #f
            (= (run-command cc "-o" binary assembly runtime "-lm") 0))))

    (define (emit-ssa compiler stdlib source output error)
      (let ((status
              (run-process*
                (list compiler "--emit-qbe" "--stdlib-root" stdlib source)
                #f output error)))
        (if (= status 0)
            (> (file-info:size (file-info output #t)) 0)
            #f)))

    (define (check-current-fixed-point root)
      (let ((build (string-append root "/build"))
            (runtime (string-append root "/build/runtime/libnpl_runtime.a"))
            (qbe (string-append root "/build/qbe/qbe"))
            (source (string-append root "/src/compiler/main.npl"))
            (stdlib (string-append root "/lib"))
            (seed (string-append root "/bootstrap/current.ssa"))
            (work (make-temporary-directory)))
        (let ((configured (= (run-command "cmake" "-S" root "-B" build
                                          "-G" "Ninja") 0))
              (built #f))
          (if configured
              (set! built (= (run-command "ninja" "-C" build
                                          "qbe_build" "npl_runtime") 0))
              #f)
          (if (not built)
              (begin
                (display "fixed-point build failed")
                (newline)
                #f)
              (let ((cc (if (get-environment-variable "NPL_CC")
                          (get-environment-variable "NPL_CC")
                          (if (get-environment-variable "CC")
                              (get-environment-variable "CC")
                              "cc")))
                  (current-bin (string-append work "/current"))
                    (gen1 (string-append work "/gen1.ssa"))
                    (gen1-bin (string-append work "/gen1"))
                    (gen2 (string-append work "/gen2.ssa"))
                    (gen2-bin (string-append work "/gen2"))
                    (gen3 (string-append work "/gen3.ssa"))
                    (error (string-append work "/error")))
                (let ((ok
                        (if (build-compiler qbe cc runtime seed current-bin
                                            work "current")
                            (if (emit-ssa current-bin stdlib source gen1 error)
                                (if (build-compiler qbe cc runtime gen1
                                                    gen1-bin work "gen1")
                                    (if (emit-ssa gen1-bin stdlib source gen2
                                                  error)
                                        (if (build-compiler qbe cc runtime
                                                            gen2 gen2-bin
                                                            work "gen2")
                                            (if (emit-ssa gen2-bin stdlib
                                                          source gen3 error)
                                                (if (file-equal? gen1 gen2)
                                                    (file-equal? gen2 gen3)
                                                    #f)
                                                #f)
                                            #f)
                                        #f)
                                    #f)
                                #f)
                            #f)))
                  (if ok
                      (begin
                        (display "current compiler fixed point: PASS")
                        (newline)
                        (display "SSA lines: ")
                        (display (line-count gen1))
                        (newline)
                        #t)
                      (begin
                        (display "current compiler fixed point: FAIL")
                        (newline)
                        (if (file-exists? error)
                            (begin (display (read-text-file error))
                                   (newline)))
                        #f))))))))
    (define (check-numeric-reference root)
      (let ((build (string-append root "/build"))
            (numeric (if (get-environment-variable "NUMERIC_STRESS")
                         (get-environment-variable "NUMERIC_STRESS")
                         (string-append root "/build/runtime/numeric_stress")))
            (reference (string-append root "/tests/numeric/reference.scm"))
            (work (make-temporary-directory)))
        (if (not (file-exists? numeric))
            (begin
              (run-command "cmake" "-S" root "-B" build "-G" "Ninja")
              (run-command "cmake" "--build" build "--target" "numeric_stress"
                           "-j" "2")))
        (let ((table-out (string-append work "/table.out"))
              (table-err (string-append work "/table.err"))
              (input (string-append work "/input"))
              (expected (string-append work "/expected"))
              (actual (string-append work "/actual"))
              (error (string-append work "/error")))
          (run-process* (list numeric) #f table-out table-err)
          (display (read-text-file table-out))
          (let ((input-status
                  (run-process* (list "guile" "--r7rs" "-q"
                                         "--no-auto-compile" "-s" reference
                                         "input")
                                   #f input error))
                (expected-status
                  (run-process* (list "guile" "--r7rs" "-q"
                                         "--no-auto-compile" "-s" reference
                                         "expected")
                                   #f expected error)))
            (if (not (if (= input-status 0) (= expected-status 0) #f))
                (begin (display "numeric reference generation failed")
                       (newline) #f)
                (let ((oracle-status
                        (run-process* (list numeric "--oracle")
                                         input actual error)))
                  (if (not (= oracle-status 0))
                      (begin (display (read-text-file error)) #f)
                      (if (not (file-equal? expected actual))
                          (begin (display "numeric oracle mismatch") (newline) #f)
                          (let loop ((operations '("quotient" "remainder"
                                                   "divide" "rational"))
                                     (ok #t))
                            (if (null? operations)
                                (if ok
                                    (begin
                                      (display "numeric Guile reference: PASS (")
                                      (display (line-count actual))
                                      (display " results; 4 zero-divisor errors)")
                                      (newline)
                                      #t)
                                    #f)
                                (let* ((operation (car operations))
                                       (out (string-append work "/zero.out"))
                                       (err (string-append work "/zero.err"))
                                       (status (run-process*
                                                 (list numeric operation "1" "0")
                                                 #f out err))
                                       (who (if (string=? operation "divide")
                                                "/" operation))
                                       (message (if (string=? operation "rational")
                                                    "zero denominator"
                                                    "division by zero"))
                                       (expected-error
                                         (string-append who ": " message
                                                        ": 1\n")))
                                  (if (if (= status 1)
                                          (if (= (file-info:size
                                                   (file-info out #t)) 0)
                                              (string=? (read-text-file err)
                                                        expected-error)
                                              #f)
                                          #f)
                                      (loop (cdr operations) ok)
                                      (begin
                                        (display "numeric zero-divisor failure: ")
                                        (display operation)
                                        (newline)
                                        (loop (cdr operations) #f))))))))))))))

    (define (split-tab line)
      (let loop ((index 0))
        (cond
          ((= index (string-length line)) (cons line ""))
          ((char=? (string-ref line index) #\tab)
           (cons (substring line 0 index)
                 (substring line (+ index 1) (string-length line))))
          (else (loop (+ index 1))))))

    (define (check-core-consistency root)
  (let* ((fixture (string-append root "/tests/fixtures/core-bindings"))
         (nplc (string-append root "/build/bin/nplc"))
         (npli (string-append root "/build/bin/npli"))
         (work (make-temporary-directory))
         (forms (text-lines
                  (read-text-file
                    (string-append fixture "/invalid-forms.tsv")))))
    (if (not (run-fixture-directory fixture nplc npli))
        #f
        (let loop ((rest forms) (count 0) (ok #t))
          (if (null? rest)
              (if ok
                  (begin
                    (display "core syntax rejection and REPL recovery: PASS (")
                    (display count)
                    (display " common cases)")
                    (newline)
                    #t)
                  #f)
              (let* ((pair (split-tab (car rest)))
                     (diagnostic (car pair))
                     (form (cdr pair))
                     (invalid (string-append work "/invalid.npl"))
                     (binary (string-append work "/invalid"))
                     (native-out (string-append work "/native.out"))
                     (native-err (string-append work "/native.err"))
                     (repl (string-append work "/repl.in"))
                     (repl-out (string-append work "/repl.out"))
                     (repl-err (string-append work "/repl.err"))
                     (guile (string-append work "/invalid.scm"))
                     (guile-out (string-append work "/guile.out"))
                     (guile-err (string-append work "/guile.err")))
                (write-text invalid
                  (string-append "(import (scheme base))\n(define (main) "
                                 form " 0)\n"))
                (write-text repl
                  (string-append "(import (scheme base) (scheme write))\n"
                                 form "\n(write 'recovered)\n(newline)\n(exit)\n"))
                (write-text guile
                  (string-append "(import (scheme base))\n" form "\n"))
                (let ((compile-status
                        (run-process* (list nplc invalid "-o" binary)
                                         #f native-out native-err))
                      (repl-status
                        (run-process* (list npli "--no-prompt")
                                         repl repl-out repl-err))
                      (guile-status
                        (run-process*
                          (list "guile" "--r7rs" "-q" "--no-auto-compile"
                                "-s" guile)
                          #f guile-out guile-err)))
                  (let ((compile-ok
                          (if (not (= compile-status 0))
                              (if (not (file-exists? binary))
                                  (if (string-contains
                                        (read-text-file native-out) diagnostic)
                                      #t
                                      (string-contains
                                        (read-text-file native-err) diagnostic))
                                  #f)
                              #f))
                        (repl-ok
                          (if (= repl-status 0)
                              (if (string=? (read-text-file repl-out)
                                            "recovered\n")
                                  (string=? (read-text-file repl-err)
                                            (string-append "error: " diagnostic
                                                           "\n"))
                                  #f)
                              #f))
                        (guile-ok
                          (if (not (= guile-status 0))
                              (if (= (file-info:size (file-info guile-out #t)) 0)
                                  (> (file-info:size (file-info guile-err #t)) 0)
                                  #f)
                              #f)))
                    (if (if compile-ok (if repl-ok guile-ok #f) #f)
                        (loop (cdr rest) (+ count 1) ok)
                        (begin
                          (display "core consistency failure: ")
                          (display diagnostic)
                          (newline)
                          (loop (cdr rest) (+ count 1) #f)))))))))))


    (define (driver-fail name)
      (display "toolchain check failed: ")
      (display name)
      (newline)
      #f)

    (define (delete-tree directory)
      (let loop ((names (directory-files directory #t)))
        (if (null? names)
            (delete-directory directory)
            (let ((path (path-join directory (car names))))
              (if (file-info-directory? (file-info path #f))
                  (delete-tree path)
                  (delete-file path))
              (loop (cdr names))))))

    (define (with-tempdir directory thunk)
      (let ((saved (get-environment-variable "TMPDIR")))
        (dynamic-wind
          (lambda () (set-environment-variable! "TMPDIR" directory))
          thunk
          (lambda ()
            (if saved
                (set-environment-variable! "TMPDIR" saved)
                (delete-environment-variable! "TMPDIR"))))))

    (define (check-driver root)
      (let* ((nplc (string-append root "/build/bin/nplc"))
             (source (string-append root "/src/compiler/tests/driver-hello.npl"))
             (extension (string-append root "/src/compiler/tests/c-extension"))
             (runtime (string-append root "/build/lib/npl/libnpl_runtime.a"))
             (version (trim-trailing-newline
                        (read-text-file (string-append root "/VERSION"))))
             (work (make-temporary-directory))
             (space (path-join work "path with spaces"))
             (tmp (path-join work "tmp"))
             (expected (path-join work "native.expected"))
             (cc (if (get-environment-variable "CC")
                     (get-environment-variable "CC")
                     "cc"))
             (ok #t))
        (define (check name condition)
          (if condition
              #t
              (begin
                (set! ok #f)
                (display "driver check failed: ")
                (display name)
                (newline))))
        (create-directory space)
        (create-directory tmp)
        (write-text expected "hello from nplc\n")
        (let ((version-out (path-join work "version"))
              (version-err (path-join work "version.err")))
          (run-process* (list nplc "--version") #f version-out version-err)
          (check "version"
                 (string=? (trim-trailing-newline (read-text-file version-out))
                           (string-append "nplc " version))))
        (let* ((spaced-source (path-join space "source program.npl"))
               (spaced-output (path-join space "native program")))
          (write-text spaced-source (read-text-file source))
          (with-tempdir tmp
            (lambda ()
              (check "spaced compile"
                     (= (run-command nplc spaced-source "-o" spaced-output) 0))
              (let ((out (path-join work "native.out")))
                (run-process* (list spaced-output) #f out
                                 (path-join work "native.err"))
                (check "spaced output" (file-equal? expected out)))))
          (with-tempdir tmp
            (lambda ()
              (with-directory
                space
                (lambda ()
                  (check "default output"
                         (= (run-command nplc "source program.npl") 0))))))
          (let ((stem-out (path-join work "stem.out")))
            (run-process* (list (path-join space "source program")) #f stem-out
                             (path-join work "stem.err"))
            (check "default stem output" (file-equal? expected stem-out)))
          (let ((stdout-ssa (path-join work "stdout.ssa"))
                (file-ssa (path-join work "file.ssa"))
                (alias-ssa (path-join work "alias.ssa")))
            (run-process* (list nplc "--emit=ssa" source) #f stdout-ssa
                             (path-join work "ssa.err"))
            (with-tempdir tmp
              (lambda ()
                (check "file SSA"
                       (= (run-command nplc "--emit=ssa" source "-o" file-ssa)
                          0))))
            (run-process* (list nplc "--emit-qbe" source) #f alias-ssa
                             (path-join work "alias.err"))
            (check "SSA stdout/file" (file-equal? stdout-ssa file-ssa))
            (check "SSA alias" (file-equal? stdout-ssa alias-ssa))
            (let ((assembly (path-join work "hello.s"))
                  (object (path-join work "hello.o"))
                  (linked (path-join work "linked")))
              (with-tempdir tmp
                (lambda ()
                  (check "assembly"
                         (= (run-command nplc "-S" source "-o" assembly) 0))
                  (check "object"
                         (= (run-command nplc "-c" source "-o" object) 0))))
              (check "assembly file" (file-exists? assembly))
              (check "object file" (file-exists? object))
              (check "link object"
                     (= (run-command cc "-o" linked object runtime "-lm") 0))
              (let ((out (path-join work "linked.out")))
                (run-process* (list linked) #f out (path-join work "linked.err"))
                (check "linked output" (file-equal? expected out)))))
          (let ((clamp-object (path-join work "clamp.o"))
                (lib-dir (path-join work "c lib"))
                (extension-out (path-join work "extension.out")))
            (create-directory lib-dir)
            (check "clamp object"
                   (= (run-command cc "-std=c99" "-Wall" "-Werror" "-I"
                                   (string-append root "/runtime") "-c" "-o"
                                   clamp-object (path-join extension "clamp.c"))
                      0))
            (check "clamp archive"
                   (= (run-command "ar" "rcs" (path-join lib-dir "libdemoclamp.a")
                                   clamp-object)
                      0))
            (with-tempdir tmp
              (lambda ()
                (check "extension link"
                       (= (run-command nplc (path-join extension "main.npl")
                                      "-L" lib-dir "-l" "democlamp" "-o"
                                      (path-join work "extension"))
                          0))))
            (run-process* (list (path-join work "extension")) #f extension-out
                             (path-join work "extension.err"))
            (check "extension output"
                   (file-equal? (path-join extension "expected.out") extension-out)))
          (let ((typed-object (path-join work "typed.o"))
                (typed-lib (path-join work "c lib")))
            (check "typed object"
                   (= (run-command cc "-std=c99" "-Wall" "-Werror" "-c" "-o"
                                   typed-object (path-join extension "typed.c"))
                      0))
            (check "typed archive"
                   (= (run-command "ar" "rcs" (path-join typed-lib "libdemotyped.a")
                                   typed-object)
                      0))
            (with-tempdir tmp
              (lambda ()
                (check "typed link"
                       (= (run-command nplc (path-join extension "typed.npl")
                                      "-L" typed-lib "-l" "demotyped" "-o"
                                      (path-join work "typed"))
                          0))))
            (let ((out (path-join work "typed.out")))
              (run-process* (list (path-join work "typed")) #f out
                               (path-join work "typed.err"))
              (check "typed output"
                     (file-equal? (path-join extension "typed.out") out)))))
        (with-directory
          "/tmp"
          (lambda ()
            (check "outside repository"
                   (= (run-command nplc source "-o" (path-join work "outside"))
                      0))))
        (let ((outside-out (path-join work "outside.out")))
          (run-process* (list (path-join work "outside")) #f outside-out
                           (path-join work "outside.err"))
          (check "outside output" (file-equal? expected outside-out)))
        (let ((rejected (path-join work "rejected"))
              (rejected-err (path-join work "rejected.err")))
          (with-tempdir tmp
            (lambda ()
              (check "compile error status"
                     (not (= (run-process*
                               (list nplc (path-join root
                                                     "src/compiler/tests/contracts/library-unknown-import.npl")
                                     "-o" rejected)
                               #f (path-join work "rejected.out") rejected-err)
                             0)))))
          (check "compile error diagnostic"
                 (string-contains (read-text-file rejected-err)
                                   "only import name not exported"))
          (check "compile error output absent" (not (file-exists? rejected))))
        (let ((qbe-err (path-join work "qbe.err")))
          (with-tempdir tmp
            (lambda ()
              (check "missing qbe status"
                     (= (run-process*
                          (list nplc "--qbe" (path-join work "missing-qbe")
                                source "-o" (path-join work "qbe-failure"))
                          #f (path-join work "qbe.out") qbe-err)
                        127))))
          (check "missing qbe diagnostic"
                 (string-contains (read-text-file qbe-err) "nplc: qbe failed")))
        (let loop ((options '("--emit=dylib" "--emit=staticlib"
                              "--emit=bogus" "--bogus")))
          (if (null? options)
              #t
              (let ((err (path-join work "option.err")))
                (check (string-append "option " (car options))
                       (= (run-process* (list nplc (car options) source)
                                           #f (path-join work "option.out") err)
                          2))
                (check (string-append "option diagnostic " (car options))
                       (> (file-info:size (file-info err #t)) 0))
                (loop (cdr options)))))
        (let ((err (path-join work "option.err")))
          (check "-l value"
                 (= (run-process* (list nplc source "-l")
                                     #f (path-join work "option.out") err)
                    2))
          (check "-l diagnostic"
                 (string-contains (read-text-file err) "nplc: -l needs a value")))
        (check "help"
               (let ((help-out (path-join work "help.out")))
                 (run-process* (list nplc "--help") #f help-out
                                  (path-join work "help.err"))
                 (string-contains (read-text-file help-out) "usage: nplc")))
        (let ((keep-err (path-join work "keep.err")))
          (with-tempdir tmp
            (lambda ()
              (check "keep temps"
                     (= (run-process*
                          (list nplc "--keep-temps" source "-o"
                                (path-join work "kept"))
                          #f (path-join work "keep.out") keep-err)
                        0))))
          (let ((kept (line-after-prefix (read-text-file keep-err)
                                         "nplc: kept temporaries in ")))
            (if kept
                (begin
                  (check "kept source"
                         (file-exists? (path-join kept "source.ssa")))
                  (check "kept assembly"
                         (file-exists? (path-join kept "source.s")))
                  (delete-tree kept))
                (check "kept directory" #f))))
        (check "no temporary leftovers" (empty-directory? tmp))
        (if ok
            (begin (display "unified nplc driver: PASS") (newline) #t)
            #f)))
)
)
