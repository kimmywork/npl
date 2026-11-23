; Toolchain checks expressed in NPL.  Each function returns #t/#f and prints
; its own evidence; tools/run-tests.npl treats them like any other suite.
(define-library (tools toolchain)
  (export check-gc check-current-fixed-point
          check-numeric-reference check-core-consistency
          run-command capture-command string-contains? file-equal?
          line-count)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (scheme write)
          (npl process)
          (npl unix)
          (npl test expect))
  (begin
    (define (string-contains? text needle)
      (let ((limit (- (string-length text) (string-length needle))))
        (let loop ((index 0))
          (cond
            ((> index limit) #f)
            ((string=? (substring text index (+ index (string-length needle)))
                       needle)
             #t)
            (else (loop (+ index 1)))))))

    (define (file-equal? left right)
      (string=? (read-text-file left) (read-text-file right)))

    (define (line-count path)
      (let ((text (read-text-file path)))
        (let loop ((index 0) (count 0))
          (if (= index (string-length text))
              count
              (if (char=? (string-ref text index) #\newline)
                  (loop (+ index 1) (+ count 1))
                  (loop (+ index 1) count))))))

    (define (run-command . arguments)
      (run-process* arguments #f #f #f))

    (define (capture-command arguments input output error)
      (run-process* arguments input output error))

    (define (temp-file directory name)
      (let ((path (string-append directory "/" name)))
        path))

    (define (cleanup-file path)
      (if (file-exists? path) (delete-file path) #f))

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
              (let* ((first-out (temp-file work "first.out"))
                     (first-err (temp-file work "first.err"))
                     (first-status
                       (capture-command (list (string-append build
                                                             "/runtime/gc_stress"))
                                        #f first-out first-err))
                     (first-text (read-text-file first-out))
                     (first-ok
                       (if (= first-status 0)
                           (string-contains? first-text
                                             "PASS: 1000000 allocations")
                           #f)))
                (display first-text)
                (set-environment-variable! "NPL_GC_NURSERY_BYTES" "65536")
                (set-environment-variable! "NPL_GC_VERIFY" "1")
                (let* ((second-out (temp-file work "second.out"))
                       (second-err (temp-file work "second.err"))
                       (second-status
                         (capture-command (list (string-append build
                                                               "/runtime/gc_stress"))
                                          #f second-out second-err))
                       (second-text (read-text-file second-out))
                       (second-ok
                         (if (= second-status 0)
                             (if (string-contains? second-text
                                                   "PASS: 1000000 allocations")
                                 (if (string-contains? second-text
                                                       " minor collections")
                                     (if (string-contains?
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
              (capture-command
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
          (capture-command (list numeric) #f table-out table-err)
          (display (read-text-file table-out))
          (let ((input-status
                  (capture-command (list "guile" "--r7rs" "-q"
                                         "--no-auto-compile" "-s" reference
                                         "input")
                                   #f input error))
                (expected-status
                  (capture-command (list "guile" "--r7rs" "-q"
                                         "--no-auto-compile" "-s" reference
                                         "expected")
                                   #f expected error)))
            (if (not (if (= input-status 0) (= expected-status 0) #f))
                (begin (display "numeric reference generation failed")
                       (newline) #f)
                (let ((oracle-status
                        (capture-command (list numeric "--oracle")
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
                                       (status (capture-command
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

    (define (split-lines text)
      (let loop ((start 0) (index 0) (lines '()))
        (cond
          ((= index (string-length text))
           (reverse (if (= start index) lines
                        (cons (substring text start index) lines))))
          ((char=? (string-ref text index) #\newline)
           (loop (+ index 1) (+ index 1)
                 (cons (substring text start index) lines)))
          (else (loop start (+ index 1) lines)))))

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
         (forms (split-lines
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
                (write-text-file invalid
                  (string-append "(import (scheme base))\n(define (main) "
                                 form " 0)\n"))
                (write-text-file repl
                  (string-append "(import (scheme base) (scheme write))\n"
                                 form "\n(write 'recovered)\n(newline)\n(exit)\n"))
                (write-text-file guile
                  (string-append "(import (scheme base))\n" form "\n"))
                (let ((compile-status
                        (capture-command (list nplc invalid "-o" binary)
                                         #f native-out native-err))
                      (repl-status
                        (capture-command (list npli "--no-prompt")
                                         repl repl-out repl-err))
                      (guile-status
                        (capture-command
                          (list "guile" "--r7rs" "-q" "--no-auto-compile"
                                "-s" guile)
                          #f guile-out guile-err)))
                  (let ((compile-ok
                          (if (not (= compile-status 0))
                              (if (not (file-exists? binary))
                                  (if (string-contains?
                                        (read-text-file native-out) diagnostic)
                                      #t
                                      (string-contains?
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

)
)
