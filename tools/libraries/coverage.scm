(begin
    ; ---- library test coverage ------------------------------------------
    ; Keep the whole symbol name in a hash set.  Export names such as
    ; file-info:size are written exactly that way in tests, so splitting
    ; names is unnecessary; the set keeps membership and memory linear.
    ; Datum labels can make a test literal cyclic (reader-datum.npl), so the
    ; walkers carry a seen set and stop at a revisited pair/vector.
    (define (coverage-add-symbols value table seen)
      (cond
        ((symbol? value)
         (hash-table-set! table (symbol->string value) #t)
         seen)
        ((pair? value)
         (if (memq value seen)
             seen
             (let ((seen (cons value seen)))
               (coverage-add-symbols
                 (cdr value) table
                 (coverage-add-symbols (car value) table seen)))))
        ((vector? value)
         (if (memq value seen)
             seen
             (let ((seen (cons value seen)))
               (let loop ((index 0) (seen seen))
                 (if (= index (vector-length value))
                     seen
                     (loop (+ index 1)
                           (coverage-add-symbols
                             (vector-ref value index) table seen)))))))
        (else seen)))

    ; Coverage means a checked occurrence, not merely a mention: symbols
    ; gathered from assertion bodies such as test-equal/test-identity/
    ; test-assert, and from fixture report values.
    (define COVERAGE-ASSERTION-HEADS
      '("test-assert" "test-equal" "test-eqv" "test-eq" "test-inexact"
        "test-identity" "test-error" "test-list" "test-string"
        "test-property" "test-property-error" "test-property-expect-fail"
        "check" "check-error" "check-true" "report" "write" "expect"
        "assert"))

    (define (coverage-assertion-head? name)
      (member name COVERAGE-ASSERTION-HEADS))

    (define (coverage-assertion-symbols value table seen)
      (cond
        ((pair? value)
         (if (memq value seen)
             seen
             (if (if (symbol? (car value))
                     (coverage-assertion-head?
                       (symbol->string (car value)))
                     #f)
                 (coverage-add-symbols value table seen)
                 (let ((seen (cons value seen)))
                   (coverage-assertion-symbols
                     (cdr value) table
                     (coverage-assertion-symbols
                       (car value) table seen))))))
        ((vector? value)
         (if (memq value seen)
             seen
             (let ((seen (cons value seen)))
               (let loop ((index 0) (seen seen))
                 (if (= index (vector-length value))
                     seen
                     (loop (+ index 1)
                           (coverage-assertion-symbols
                             (vector-ref value index) table seen)))))))
        (else seen)))

    (define (coverage-test-symbols paths)
      (let ((table (make-hash-table string=?)))
        (let loop ((rest paths))
          (if (null? rest)
              table
              (begin
                (coverage-assertion-symbols
                  (read-forms (car rest)) table '())
                (loop (cdr rest)))))))

    (define (coverage-missing names symbols)
      (let loop ((rest names) (out '()))
        (if (null? rest)
            (reverse out)
            (loop (cdr rest)
                  (if (hash-table-contains? symbols (car rest))
                      out
                      (cons (car rest) out))))))

    (define (coverage-source? path)
      (if (extension path ".npl") #t (extension path ".scm")))

    (define (coverage-source-files directory)
      (let loop ((names (directory-files directory #t)) (files '()))
        (if (null? names)
            files
            (let ((path (path-join directory (car names))))
              (if (file-info-directory? (file-info path #f))
                  (loop (cdr names)
                        (append (coverage-source-files path) files))
                  (if (coverage-source? path)
                      (loop (cdr names) (cons path files))
                      (loop (cdr names) files)))))))

    (define (coverage-sld-files directory)
      (let loop ((names (directory-files directory #t)) (files '()))
        (if (null? names)
            files
            (let ((path (path-join directory (car names))))
              (if (file-info-directory? (file-info path #f))
                  (loop (cdr names)
                        (append (coverage-sld-files path) files))
                  (if (extension path ".sld")
                      (loop (cdr names) (cons path files))
                      (loop (cdr names) files)))))))

    ; The auto-coverage pass uses fixture/property/unit tests and valid
    ; compiler suites.  Negative reader/contract/import fixtures are excluded
    ; because they are intentionally malformed.
    (define (coverage-compiler-source-files directory)
      (let loop ((names (directory-files directory #t)) (files '()))
        (if (null? names)
            files
            (let ((name (car names))
                  (path (path-join directory (car names))))
              (if (file-info-directory? (file-info path #f))
                  (if (member name '("reader-errors" "macro-errors"
                                     "contracts" "imports"))
                      (loop (cdr names) files)
                      (loop (cdr names)
                            (append (coverage-compiler-source-files path)
                                    files)))
                  (if (coverage-source? path)
                      (loop (cdr names) (cons path files))
                      (loop (cdr names) files)))))))

    ; Only test code counts: unit tests, expect fixtures, property tests,
    ; the compiler's suites, the evaluator's core tests, and the library
    ; checker's tests.  The gate's own
    ; assertion list (tools/run-tests.npl `report` forms) is the test for
    ; the tools/*.sld checks.  Implementation code that merely calls a name
    ; (tools, src/repl, lib) never counts.
    (define (coverage-all-test-files root)
      (append
        (coverage-source-files (path-join root "tests/fixtures"))
        (coverage-source-files (path-join root "tests/property"))
        (coverage-source-files (path-join root "tests/unit"))
        (coverage-compiler-source-files
          (path-join root "src/compiler/tests"))
        (list (path-join root "src/evaluator/core-tests.npl")
              (path-join root "tools/libraries-tests.npl")
              (path-join root "tools/ci-changes-tests.npl")
              (path-join root "tools/run-tests.npl"))))

    (define (coverage-percent part total)
      (if (= total 0) 0 (quotient (* part 100) total)))

    ; Only exports that are defined in this library (including its included
    ; files) count.  Re-exports from a dependency have no local definition.
    (define (coverage-local-exports path)
      (let* ((parsed (parse-library path))
             (exports (map export-internal-name (car parsed)))
             (body (car (cdr (cdr parsed))))
             (defs (definition-name-list
                     (collect-defs body (path-directory path)))))
        (let loop ((rest exports) (out '()))
          (if (null? rest)
              (reverse out)
              (if (member (car rest) defs)
                  (loop (cdr rest) (cons (car rest) out))
                  (loop (cdr rest) out))))))

    (define COVERAGE-TARGET 80)
    (define COVERAGE-BASELINE "tools/libraries/coverage-baseline.scm")

    (define (coverage-library-key path)
      (library-name-string (car (cdr (library-form (read-forms path))))))

    ; One entry per library with local exports:
    ;   (key covered total missing-names)
    (define (coverage-symbol-entries paths symbols)
      (let loop ((rest paths) (out '()))
        (if (null? rest)
            (reverse out)
            (let ((local (coverage-local-exports (car rest))))
              (if (null? local)
                  (loop (cdr rest) out)
                  (let ((missing (coverage-missing local symbols)))
                    (loop (cdr rest)
                          (cons (list (coverage-library-key (car rest))
                                      (- (length local) (length missing))
                                      (length local)
                                      missing)
                                out))))))))

    (define (coverage-below-target? covered total)
      (< (* covered 100) (* total COVERAGE-TARGET)))

    ; The baseline is the ratchet for libraries still below the target:
    ;   (overall covered total)
    ;   (library KEY missing-name ...)
    ; A name missing now but not listed is a regression (a new export without
    ; a test, or a test that stopped asserting it).  A listed name that is
    ; covered now must be removed, so gains are kept.  The overall ratio may
    ; not fall below the recorded one.
    (define (coverage-read-baseline root)
      (let ((path (path-join root COVERAGE-BASELINE)))
        (if (file-exists? path) (read-forms path) '())))

    (define (coverage-baseline-gaps baseline key)
      (let loop ((rest baseline))
        (cond
          ((null? rest) #f)
          ((if (eq? (car (car rest)) 'library)
               (string=? (name-string (car (cdr (car rest)))) key)
               #f)
           (map name-string (cdr (cdr (car rest)))))
          (else (loop (cdr rest))))))

    (define (coverage-baseline-overall baseline)
      (let ((entry (assq 'overall baseline)))
        (if entry (cdr entry) #f)))

    (define (coverage-print-names label names)
      (display label)
      (for-each (lambda (name) (display " ") (display name)) names)
      (newline))

    (define (coverage-check-entry entry baseline)
      (let* ((key (car entry))
             (covered (car (cdr entry)))
             (total (car (cdr (cdr entry))))
             (missing (car (cdr (cdr (cdr entry)))))
             (gaps (coverage-baseline-gaps baseline key))
             (below (coverage-below-target? covered total))
             (new-gaps (let loop ((rest missing) (out '()))
                         (cond
                           ((null? rest) (reverse out))
                           ((if gaps (member (car rest) gaps) #f)
                            (loop (cdr rest) out))
                           (else (loop (cdr rest) (cons (car rest) out))))))
             (closed (if gaps
                         (let loop ((rest gaps) (out '()))
                           (cond
                             ((null? rest) (reverse out))
                             ((member (car rest) missing) (loop (cdr rest) out))
                             (else (loop (cdr rest) (cons (car rest) out)))))
                         '()))
             (failed #f))
        (if below
            (begin
              (display "  ")
              (display key)
              (display ": ")
              (display covered)
              (display "/")
              (display total)
              (display " (")
              (display (coverage-percent covered total))
              (display "%, target ")
              (display COVERAGE-TARGET)
              (display "%)")
              (newline)))
        ; A library at the target owes nothing; below it, no new gap.
        (if (if below (pair? new-gaps) #f)
            (begin
              (set! failed #t)
              (coverage-print-names
                "    coverage decreased; add tests for:" new-gaps)))
        (if (pair? closed)
            (begin
              (set! failed #t)
              (coverage-print-names
                (string-append "    covered now; remove from "
                               COVERAGE-BASELINE ":")
                closed)))
        (if (if gaps (not below) #f)
            (begin
              (set! failed #t)
              (display "  ")
              (display key)
              (display " reached the target; remove its entry from ")
              (display COVERAGE-BASELINE)
              (newline)))
        (not failed)))

    (define (coverage-check-symbols entries baseline)
      (display "symbol coverage (target ")
      (display COVERAGE-TARGET)
      (display "% per library; known gaps in ")
      (display COVERAGE-BASELINE)
      (display "):")
      (newline)
      (let* ((ok (let loop ((rest entries) (ok #t))
                   (if (null? rest)
                       ok
                       (loop (cdr rest)
                             (if (coverage-check-entry (car rest) baseline)
                                 ok
                                 #f)))))
             (covered (apply + (map (lambda (e) (car (cdr e))) entries)))
             (total (apply + (map (lambda (e) (car (cdr (cdr e)))) entries)))
             (recorded (coverage-baseline-overall baseline))
             (overall-ok
               (if recorded
                   ; covered/total >= recorded-covered/recorded-total
                   (>= (* covered (car (cdr recorded)))
                       (* (car recorded) total))
                   #t)))
        (display "overall symbol coverage: ")
        (display covered)
        (display "/")
        (display total)
        (display " (")
        (display (coverage-percent covered total))
        (display "%)")
        (if recorded
            (begin
              (display "; recorded ")
              (display (car recorded))
              (display "/")
              (display (car (cdr recorded)))))
        (newline)
        (if (not overall-ok)
            (begin
              (display "  overall coverage decreased")
              (newline)))
        (if ok overall-ok #f)))

    ; tools/export-origins.npl --coverage-baseline writes the current gaps.
    (define (write-coverage-baseline root)
      (let* ((entries (coverage-symbol-entries (coverage-library-paths root)
                                               (coverage-test-symbols
                                                 (coverage-all-test-files root))))
             (covered (apply + (map (lambda (e) (car (cdr e))) entries)))
             (total (apply + (map (lambda (e) (car (cdr (cdr e)))) entries))))
        (call-with-output-file (path-join root COVERAGE-BASELINE)
          (lambda (port)
            (display "; Library test-coverage ratchet (tools/libraries/coverage.scm).\n" port)
            (display ";\n" port)
            (display "; Libraries below the 80% target list the exports that have no\n" port)
            (display "; unit/expect/property assertion yet.  The gate fails when a library\n" port)
            (display "; gains a gap not listed here, when a listed gap is covered (remove\n" port)
            (display "; it), or when the overall ratio falls below (overall covered total).\n" port)
            (write (list 'overall covered total) port)
            (newline port)
            (for-each
              (lambda (entry)
                (if (coverage-below-target? (car (cdr entry))
                                            (car (cdr (cdr entry))))
                    (begin
                      (write (cons 'library
                                   (cons (string->symbol (car entry))
                                         (map string->symbol
                                              (car (cdr (cdr (cdr entry)))))))
                             port)
                      (newline port))))
              entries)))
        #t))

    (define (coverage-present? names symbols)
      (if (null? names)
          #f
          (if (hash-table-contains? symbols (car names))
              #t
              (coverage-present? (cdr names) symbols))))

    (define (coverage-status names symbols)
      (if (null? (coverage-missing names symbols))
          "full"
          (if (coverage-present? names symbols) "partial" "uncovered")))

    ; Library coverage is derived from the same scanned local exports:
    ; re-export-only libraries have nothing of their own to check and are
    ; skipped.  "full" means every local export is asserted, "partial" some,
    ; "uncovered" none.  The uncovered ratio must stay below 30%.
    (define (coverage-library-report paths symbols)
      (let loop ((rest paths) (total 0) (covered 0) (full 0)
                 (uncovered '()) (details '()) (skipped 0))
        (if (null? rest)
            (list total covered full (reverse uncovered)
                  (reverse details) skipped)
            (let* ((path (car rest))
                   (local (coverage-local-exports path)))
              (if (null? local)
                  (loop (cdr rest) total covered full uncovered details
                        (+ skipped 1))
                  (let ((status (coverage-status local symbols)))
                    (loop (cdr rest) (+ total 1)
                          (if (string=? status "uncovered")
                              covered (+ covered 1))
                          (if (string=? status "full") (+ full 1) full)
                          (if (string=? status "uncovered")
                              (cons path uncovered) uncovered)
                          (cons (cons path status) details)
                          skipped)))))))

    (define (coverage-library-paths root)
      (append
        (coverage-sld-files (path-join root "lib/npl"))
        (coverage-sld-files (path-join root "lib/scheme"))
        (coverage-sld-files (path-join root "tools"))))

    (define (check-library-test-coverage root)
      (let* ((paths (coverage-library-paths root))
             (symbols (coverage-test-symbols (coverage-all-test-files root)))
             (report (coverage-library-report paths symbols))
             (total (car report))
             (covered (car (cdr report)))
             (full (car (cdr (cdr report))))
             (uncovered (car (cdr (cdr (cdr report)))))
             (skipped (car (cdr (cdr (cdr (cdr (cdr report)))))))
             (uncovered-count (length uncovered))
             (symbol-ok (coverage-check-symbols
                          (coverage-symbol-entries paths symbols)
                          (coverage-read-baseline root))))
        (display "libraries with local exports: ")
        (display total)
        (display " (full ")
        (display full)
        (display ", partial ")
        (display (- covered full))
        (display ", uncovered ")
        (display uncovered-count)
        (display " = ")
        (display (coverage-percent uncovered-count total))
        (display "%, limit 30%); re-export-only skipped: ")
        (display skipped)
        (newline)
        (if (if symbol-ok
                (if (= total 0)
                    #t
                    (< (* uncovered-count 100) (* total 30)))
                #f)
            (begin
              (display "library test coverage: PASS")
              (newline)
              #t)
            (begin
              (display "library test coverage: FAILED")
              (newline)
              #f))))
)
