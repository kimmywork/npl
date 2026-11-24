(begin
    ; ---- final export origins -------------------------------------------
    ; Group every external name by the final origin resolved through the
    ; import graph.  An origin is "define:KEY#NAME",
    ; "define-c-primitive:KEY#NAME", "define-c-function:KEY#NAME", or
    ; "primitive:NAME".  Re-exporting the same origin is not a conflict;
    ; different origins are reported for review.
    (define (export-bindings entries key out)
      (if (null? entries)
          out
          (export-bindings
            (cdr entries) key
            (cons (list (car (car entries)) key (cdr (car entries))) out))))

    (define (collect-export-bindings cache out)
      (if (null? cache)
          out
          (let* ((entry (car cache))
                 (key (car entry))
                 (exports (car (cdr entry))))
            (collect-export-bindings
              (cdr cache) (export-bindings exports key out)))))

    (define (export-binding-group name bindings)
      (if (null? bindings)
          ()
          (if (string=? name (car (car bindings)))
              (cons (car bindings)
                    (export-binding-group name (cdr bindings)))
              (export-binding-group name (cdr bindings)))))

    (define (origin-entry entry)
      (cons (car (cdr entry)) (car (cdr (cdr entry)))))

    (define (origin-known? origin origins)
      (if (null? origins)
          #f
          (if (string=? (cdr origin) (cdr (car origins)))
              #t
              (origin-known? origin (cdr origins)))))

    (define (distinct-origins entries origins)
      (if (null? entries)
          origins
          (let ((origin (origin-entry (car entries))))
            (distinct-origins
              (cdr entries)
              (if (origin-known? origin origins)
                  origins
                  (cons origin origins))))))

    ; "kind:KEY#NAME" -> KEY, "primitive:NAME" -> "primitive".
    (define (origin-library origin)
      (let ((colon (let loop ((index 0))
                     (cond ((= index (string-length origin)) #f)
                           ((char=? (string-ref origin index) #\:) index)
                           (else (loop (+ index 1))))))
            (hash (let loop ((index (- (string-length origin) 1)))
                    (cond ((< index 0) #f)
                          ((char=? (string-ref origin index) #\#) index)
                          (else (loop (- index 1)))))))
        (if (if colon hash #f)
            (substring origin (+ colon 1) hash)
            "primitive")))

    (define (sorted-strings items)
      (let loop ((rest items) (out '()))
        (if (null? rest)
            out
            (loop (cdr rest)
                  (let insert ((sorted out))
                    (cond
                      ((null? sorted) (list (car rest)))
                      ((string=? (car rest) (car sorted)) sorted)
                      ((string<? (car rest) (car sorted))
                       (cons (car rest) sorted))
                      (else (cons (car sorted) (insert (cdr sorted))))))))))

    ; ---- reviewed exceptions ----
    ; tools/libraries/export-origin-exceptions.scm lists every name allowed
    ; to reach more than one definition, with the defining libraries:
    ;   (string-hash npl/hashtable npl/string)
    ; Only names a standard (srfi/*, scheme/*) library exports may be listed;
    ; any other overlap must be renamed.  A listed name that no longer
    ; conflicts, or gained a definition, fails too, so the list only shrinks.
    (define EXPORT-ORIGIN-EXCEPTIONS "tools/libraries/export-origin-exceptions.scm")

    (define (read-origin-exceptions root)
      (let ((path (path-join root EXPORT-ORIGIN-EXCEPTIONS)))
        (if (file-exists? path)
            (map (lambda (entry)
                   (cons (name-string (car entry))
                         (sorted-strings (map library-name-text (cdr entry)))))
                 (read-forms path))
            '())))

    (define (library-name-text value)
      (if (symbol? value) (symbol->string value) value))

    (define (standard-library-key? key)
      (if (string-prefix? "srfi/" key)
          #t
          (if (string-prefix? "scheme/" key)
              (not (string-prefix? "scheme/internal/" key))
              #f)))

    ; The standard libraries that export NAME, e.g. ("scheme/base" "srfi/44").
    (define (standard-exporters name)
      (let loop ((rest LIBRARY-CACHE) (out '()))
        (if (null? rest)
            (sorted-strings out)
            (loop (cdr rest)
                  (if (if (standard-library-key? (car (car rest)))
                          (assoc name (car (cdr (car rest))))
                          #f)
                      (cons (car (car rest)) out)
                      out)))))

    (define (display-libraries libraries)
      (for-each (lambda (library)
                  (display " (")
                  (display (slash->space library))
                  (display ")"))
                libraries))

    (define (slash->space text)
      (let ((out (string-copy text)))
        (let loop ((index 0))
          (if (= index (string-length out))
              out
              (begin
                (if (char=? (string-ref out index) #\/)
                    (string-set! out index #\space))
                (loop (+ index 1)))))))

    ; One conflicting name: 0 when a reviewed exception covers it, 1 when
    ; it is new or changed.  Allowed overlaps are printed as notes.
    (define (report-export-origin-group name entries exceptions)
      (let* ((origins (distinct-origins entries ()))
             (libraries (sorted-strings
                          (map (lambda (origin) (origin-library (cdr origin)))
                               origins)))
             (exception (assoc name exceptions)))
        (cond
          ((<= (length origins) 1) 0)
          ((if exception (equal? (cdr exception) libraries) #f)
           (display "note: ")
           (display name)
           (display " has different definitions in")
           (display-libraries libraries)
           (display "; specified by")
           (display-libraries (standard-exporters name))
           (newline)
           0)
          (else
           (let ((standard (standard-exporters name)))
             (display "export conflict: ")
             (display name)
             (display " has different definitions in")
             (display-libraries libraries)
             (newline)
             (for-each
               (lambda (origin)
                 (display "  ")
                 (display (car origin))
                 (display " -> ")
                 (display (cdr origin))
                 (newline))
               (reverse origins))
             (if (null? standard)
                 (begin
                   (display "  not a SRFI/R7RS name: rename one definition")
                   (newline))
                 (begin
                   (display "  specified by")
                   (display-libraries standard)
                   (display "; if intentional, add (")
                   (display name)
                   (for-each (lambda (library)
                               (display " ")
                               (display library))
                             libraries)
                   (display ") to ")
                   (display EXPORT-ORIGIN-EXCEPTIONS)
                   (newline))))
           1))))

    (define (check-export-origins root)
      (set! LIBRARY-FAILURES '())
      (resolve-all root)
      (if (not (null? LIBRARY-FAILURES))
          (begin (report-failures) #f)
          (let ((bindings (collect-export-bindings LIBRARY-CACHE '()))
                (exceptions (read-origin-exceptions root))
                (seen '())
                (conflicting '())
                (failures 0))
            (let loop ((rest bindings))
              (if (null? rest)
                  #t
                  (let ((name (car (car rest))))
                    (if (member name seen)
                        (loop (cdr rest))
                        (let ((group (export-binding-group name bindings)))
                          (set! seen (cons name seen))
                          (if (> (length (distinct-origins group ())) 1)
                              (set! conflicting (cons name conflicting)))
                          (set! failures
                                (+ failures
                                   (report-export-origin-group
                                     name group exceptions)))
                          (loop (cdr rest)))))))
            ; Stale exceptions: the overlap was removed, so drop the entry.
            (for-each
              (lambda (exception)
                (if (not (member (car exception) conflicting))
                    (begin
                      (set! failures (+ failures 1))
                      (display "stale export-origin exception: ")
                      (display (car exception))
                      (display " no longer has different definitions; remove it from ")
                      (display EXPORT-ORIGIN-EXCEPTIONS)
                      (newline))))
              exceptions)
            (if (= failures 0)
                (begin
                  (display "export origins: PASS (")
                  (display (length conflicting))
                  (display " reviewed SRFI/R7RS overlaps, limit ")
                  (display (length exceptions))
                  (display ")")
                  (newline)
                  #t)
                (begin
                  (display "export origins: FAILED (")
                  (display failures)
                  (display " new, changed, or stale)")
                  (newline)
                  #f)))))
)
