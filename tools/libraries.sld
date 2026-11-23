; Library metadata checks: exports, imports, cycles, and layering.
;
; The scanner reads every .sld under lib/, follows include files, and derives
; the binding graph from the source itself.  It does not keep an alias table:
; an export is valid when it is defined once locally or resolves through an
; import (including C/primitive roots).
(define-library (tools libraries)
  (export check-libraries)
  (import (scheme base)
          (scheme read)
          (scheme file)
          (scheme write)
          (npl unix))
  (begin
    (define LIBRARY-FILES '())
    (define LIBRARY-CACHE '())
    (define LIBRARY-VISITING '())
    (define FAILURES '())
    (define FILE-COUNT 0)
    (define SCM-COUNT 0)

    (define (string-prefix? prefix text)
      (if (> (string-length prefix) (string-length text))
          #f
          (string=? prefix (substring text 0 (string-length prefix)))))

    (define (path-directory path)
      (let loop ((index (- (string-length path) 1)))
        (cond
          ((< index 0) "")
          ((char=? (string-ref path index) #\/)
           (substring path 0 (+ index 1)))
          (else (loop (- index 1))))))

    (define (path-join left right)
      (if (if (= (string-length left) 0)
              #t
              (char=? (string-ref left (- (string-length left) 1)) #\/))
          (string-append left right)
          (string-append left "/" right)))

    (define (extension path suffix)
      (let ((offset (- (string-length path) (string-length suffix))))
        (if (< offset 0) #f (string=? (substring path offset
                                                  (string-length path))
                                      suffix))))

    (define (walk-files directory)
      (let loop ((names (directory-files directory #t)) (files '()))
        (if (null? names)
            files
            (let ((path (path-join directory (car names))))
              (if (file-info-directory? (file-info path #f))
                  (loop (cdr names) (append (walk-files path) files))
                  (begin
                    (set! FILE-COUNT (+ FILE-COUNT 1))
                    (if (if (extension path ".scm") #t (extension path ".npl"))
                        (set! SCM-COUNT (+ SCM-COUNT 1)))
                    (loop (cdr names) (cons path files))))))))

    (define (read-forms path)
      (call-with-input-file path
        (lambda (port)
          (let loop ((forms '()))
            (let ((form (read port)))
              (if (eof-object? form)
                  (reverse forms)
                  (loop (cons form forms))))))))

    (define (library-name-string name)
      (let loop ((parts name) (out ""))
        (if (null? parts)
            out
            (let ((part (if (symbol? (car parts))
                            (symbol->string (car parts))
                            (number->string (car parts)))))
              (loop (cdr parts)
                    (if (string=? out "")
                        part
                        (string-append out "/" part)))))))

    (define (name-string name)
      (if (symbol? name) (symbol->string name)
          (if (number? name) (number->string name) "")))

    (define (library-form forms)
      (let loop ((rest forms))
        (cond
          ((null? rest) #f)
          ((if (pair? (car rest))
               (eq? (car (car rest)) 'define-library)
               #f)
           (car rest))
          (else (loop (cdr rest))))))

    (define (library-file-key root path)
      (let ((prefix (path-join root "lib")))
        (substring path (+ (string-length prefix) 1)
                   (- (string-length path) (string-length ".sld")))))

    (define (scan-libraries root)
      (set! LIBRARY-FILES '())
      (set! FILE-COUNT 0)
      (set! SCM-COUNT 0)
      (let loop ((paths (walk-files (path-join root "lib"))))
        (if (null? paths)
            0
            (let ((path (car paths)))
              (if (extension path ".sld")
                  (let ((form (library-form (read-forms path))))
                    (if (not form)
                        (add-failure (string-append path ": no define-library"))
                        (let* ((name (car (cdr form)))
                               (key (library-name-string name)))
                          (if (not (string=? key (library-file-key root path)))
                              (add-failure
                                (string-append path ": library name " key
                                               " does not match path"))
                              (set! LIBRARY-FILES
                                    (cons (cons key path) LIBRARY-FILES))))))
                  #f)
              (loop (cdr paths)))))
      LIBRARY-FILES)

    (define (add-failure message)
      (set! FAILURES (cons message FAILURES))
      #f)

    (define (report-failures)
      (let loop ((rest (reverse FAILURES)))
        (if (null? rest)
            0
            (begin
              (display "library check: ")
              (display (car rest))
              (newline)
              (loop (cdr rest))))))

    (define (file-for-key key)
      (let ((entry (assoc key LIBRARY-FILES)))
        (if entry (cdr entry) #f)))

    (define (declaration-head form)
      (if (pair? form) (car form) #f))

    (define (parse-library path)
      (let* ((form (library-form (read-forms path)))
             (name (car (cdr form)))
             (declarations (cdr (cdr form)))
             (base (path-directory path)))
        (let loop ((rest declarations)
                   (exports '())
                   (imports '())
                   (body '()))
          (if (null? rest)
              (list exports imports body)
              (let ((declaration (car rest))
                    (head (declaration-head (car rest))))
                (cond
                  ((eq? head 'export)
                   (loop (cdr rest) (append exports (cdr declaration)) imports
                         body))
                  ((eq? head 'import)
                   (loop (cdr rest) exports (append imports (cdr declaration))
                         body))
                  ((eq? head 'begin)
                   (loop (cdr rest) exports imports
                         (append body (cdr declaration))))
                  ((if (eq? head 'include) #t (eq? head 'include-ci))
                   (loop (cdr rest) exports imports
                         (append body
                                 (read-included (cdr declaration) base))))
                  ((eq? head 'cond-expand)
                   (loop (cdr rest)
                         (append exports (cond-exports (cdr declaration) base))
                         (append imports (cond-imports (cdr declaration) base))
                         (append body (cond-body (cdr declaration) base))))
                  (else (loop (cdr rest) exports imports body))))))))

    (define (read-included paths base)
      (let loop ((rest paths) (forms '()))
        (if (null? rest)
            forms
            (loop (cdr rest)
                  (append forms
                          (read-forms (path-join base (car rest))))))))

    (define (cond-exports clauses base)
      (let loop ((rest clauses) (out '()))
        (if (null? rest) out
            (loop (cdr rest)
                  (append out (declaration-part (cdr (car rest)) 'export))))))
    (define (cond-imports clauses base)
      (let loop ((rest clauses) (out '()))
        (if (null? rest) out
            (loop (cdr rest)
                  (append out (declaration-part (cdr (car rest)) 'import))))))
    (define (cond-body clauses base)
      (let loop ((rest clauses) (out '()))
        (if (null? rest) out
            (loop (cdr rest)
                  (append out (declaration-part (cdr (car rest)) 'body))))))
    (define (declaration-part forms kind)
      (let loop ((rest forms) (out '()))
        (if (null? rest) out
            (let ((form (car rest)))
              (if (pair? form)
                  (if (eq? (car form) kind)
                      (loop (cdr rest) (append out (cdr form)))
                      (if (eq? (car form) 'begin)
                          (loop (cdr rest) (append out (cdr form)))
                          (loop (cdr rest) out)))
                  (loop (cdr rest) out))))))

    ; ---- definitions -----------------------------------------------------

    (define (collect-defs forms base)
      (let loop ((rest forms) (defs '()))
        (if (null? rest)
            defs
            (loop (cdr rest) (append (form-defs (car rest) base) defs)))))

    (define (form-defs form base)
      (if (not (pair? form))
          '()
          (let ((head (car form)))
            (cond
              ((eq? head 'define)
               (if (pair? (cdr form))
                   (let ((target (car (cdr form))))
                     (if (pair? target)
                         (list (name-string (car target)))
                         (list (name-string target))))
                   '()))
              ((eq? head 'define-syntax)
               (if (pair? (cdr form))
                   (list (name-string (car (cdr form))))
                   '()))
              ((eq? head 'define-values)
               (if (pair? (cdr form))
                   (let ((names (car (cdr form))))
                     (if (pair? names)
                         (map name-string (cdr names))
                         (list (name-string names))))
                   '()))
              ((eq? head 'define-record-type)
               (record-defs form))
              ((eq? head 'define-c-primitive)
               (if (if (pair? (cdr form)) (pair? (car (cdr form))) #f)
                   (list (name-string (car (car (cdr form)))))
                   '()))
              ((eq? head 'define-c-function)
               (if (pair? (cdr form))
                   (list (name-string (car (cdr form))))
                   '()))
              ((eq? head 'begin)
               (collect-defs (cdr form) base))
              ((if (eq? head 'include) #t (eq? head 'include-ci))
               (collect-defs (read-included (cdr form) base) base))
              ((eq? head 'cond-expand)
               (collect-cond-defs (cdr form) base))
              (else '())))))

    (define (record-defs form)
      (if (< (length form) 4)
          '()
          (let ((name (name-string (car (cdr form))))
                (constructor (car (cdr (cdr form))))
                (predicate (car (cdr (cdr (cdr form)))))
                (fields (cdr (cdr (cdr (cdr form))))))
            (let loop ((rest fields) (out (list name)))
              (if (null? rest)
                  (cons (name-string predicate)
                        (cons (if (pair? constructor)
                                  (name-string (car constructor))
                                  (name-string constructor))
                              out))
                  (let ((field (car rest)))
                    (loop (cdr rest)
                          (if (if (pair? field) (pair? (cdr field)) #f)
                              (cons (name-string (car (cdr field))) out)
                              out))))))))

    (define (collect-cond-defs clauses base)
      (let loop ((rest clauses) (out '()))
        (if (null? rest)
            out
            (loop (cdr rest)
                  (append out (collect-defs (cdr (car rest)) base))))))

    (define (count-defs defs)
      (let loop ((rest defs) (counts '()))
        (if (null? rest)
            counts
            (let* ((name (car rest))
                   (entry (assoc name counts)))
              (if entry
                  (begin
                    (set-cdr! entry (+ (cdr entry) 1))
                    (loop (cdr rest) counts))
                  (loop (cdr rest) (cons (cons name 1) counts)))))))

    ; ---- import and export resolution -----------------------------------

    (define (base-name? form)
      (if (pair? form)
          (let ((head (car form)))
            (if (eq? head 'only) #f
                (if (eq? head 'except) #f
                    (if (eq? head 'prefix) #f
                        (if (eq? head 'rename) #f #t)))))
          #f))

    (define (primitive-key? key) (string=? key "npl/internal/primitive"))

    (define (resolve-import-set set source)
      (if (not (pair? set))
          (error "malformed import set" set)
          (let ((head (car set)))
            (cond
              ((eq? head 'only)
               (let ((base (resolve-import-set (car (cdr set)) source))
                     (names (map name-string (cdr (cdr set)))))
                 (cons (filter-imports (car base) names) (cdr base))))
              ((eq? head 'except)
               (let ((base (resolve-import-set (car (cdr set)) source))
                     (names (map name-string (cdr (cdr set)))))
                 (cons (remove-imports (car base) names) (cdr base))))
              ((eq? head 'prefix)
               (let ((base (resolve-import-set (car (cdr set)) source))
                     (prefix (name-string (car (cdr (cdr set))))))
                 (cons (map (lambda (entry)
                              (cons (string-append prefix (car entry))
                                    (cdr entry)))
                            (car base))
                       (cdr base))))
              ((eq? head 'rename)
               (let ((base (resolve-import-set (car (cdr set)) source))
                     (renames (cdr (cdr set))))
                 (cons (rename-imports (car base) renames) (cdr base))))
              (else
               (let ((key (library-name-string set)))
                 (if (primitive-key? key)
                     (cons '() #t)
                     (let ((library (resolve-library key)))
                       (check-layer source key)
                       (cons (car library) #f)))))))))

    (define (filter-imports entries names)
      (let loop ((rest entries) (out '()))
        (if (null? rest) out
            (if (member (car (car rest)) names)
                (loop (cdr rest) (cons (car rest) out))
                (loop (cdr rest) out)))))

    (define (remove-imports entries names)
      (let loop ((rest entries) (out '()))
        (if (null? rest) out
            (if (member (car (car rest)) names)
                (loop (cdr rest) out)
                (loop (cdr rest) (cons (car rest) out))))))

    (define (rename-for name renames)
      (let loop ((rest renames))
        (cond
          ((null? rest) #f)
          ((string=? name (name-string (car (car rest)))) (car rest))
          (else (loop (cdr rest))))))

    (define (rename-imports entries renames)
      (let loop ((rest entries) (out '()))
        (if (null? rest) out
            (let* ((entry (car rest))
                   (rename (rename-for (car entry) renames)))
              (loop (cdr rest)
                    (if rename
                        (cons (cons (name-string (car (cdr rename)))
                                    (cdr entry))
                              out)
                        (cons entry out)))))))

    (define (merge-imports left right key)
      (let loop ((rest right) (out left))
        (if (null? rest)
            out
            (let* ((entry (car rest))
                   (old (assoc (car entry) out)))
              (if old
                  (if (string=? (cdr old) (cdr entry))
                      (loop (cdr rest) out)
                      (error (string-append "import conflict in " key ": "
                                            (car entry))
                             (cdr old) (cdr entry)))
                  (loop (cdr rest) (cons entry out)))))))

    (define (import-forms forms key)
      (let loop ((rest forms) (entries '()) (primitive #f))
        (if (null? rest)
            (cons entries primitive)
            (let ((resolved (resolve-import-set (car rest) key)))
              (loop (cdr rest)
                    (merge-imports entries (car resolved) key)
                    (if primitive #t (cdr resolved)))))))

    (define (layer key)
      (cond
        ((string-prefix? "npl/internal/" key) 0)
        ((string-prefix? "scheme/" key) 1)
        ((string-prefix? "npl/demo/" key) 3)
        ((string-prefix? "npl/" key) 2)
        ((string-prefix? "srfi/" key) 4)
        (else 5)))

    (define (layer-ok? source target)
      (let ((source-layer (layer source))
            (target-layer (layer target)))
        (cond
          ((= source-layer 0) (= target-layer 0))
          ((= source-layer 1) (<= target-layer 1))
          ((= source-layer 2) (<= target-layer 2))
          ((= source-layer 3) (<= target-layer 2))
          ((= source-layer 4) (<= target-layer 2))
          (else #t))))

    (define (check-layer source target)
      (if (not (layer-ok? source target))
          (error (string-append "layer violation: " source " imports " target)))
      #t)

    (define (resolve-library key)
      (let ((cached (assoc key LIBRARY-CACHE)))
        (if cached
            (cdr cached)
            (if (member key LIBRARY-VISITING)
                (error (string-append "library cycle: " key))
                (let ((path (file-for-key key)))
                  (if (not path)
                      (error (string-append "missing library file: " key))
                      (let* ((parsed (parse-library path))
                             (exports (car parsed))
                             (imports (car (cdr parsed)))
                             (body (car (cdr (cdr parsed))))
                             (imported (import-forms imports key))
                             (defs (count-defs
                                     (collect-defs body (path-directory path))))
                             (result (build-exports key exports
                                                    (car imported)
                                                    (cdr imported)
                                                    defs)))
                        (set! LIBRARY-VISITING
                              (remove-string key LIBRARY-VISITING))
                        (set! LIBRARY-CACHE
                              (cons (cons key result) LIBRARY-CACHE))
                        result)))))))


    (define (remove-string item items)
      (let loop ((rest items) (out '()))
        (if (null? rest) out
            (if (string=? item (car rest))
                (loop (cdr rest) out)
                (loop (cdr rest) (cons (car rest) out))))))

    (define (definition-count name defs)
      (let ((entry (assoc name defs)))
        (if entry (cdr entry) 0)))

    (define (build-exports key exports imported primitive defs)
      (let loop ((rest exports) (out '()))
        (if (null? rest)
            (cons (reverse out) '())
            (let* ((entry (car rest))
                   (rename? (if (pair? entry) (eq? (car entry) 'rename) #f))
                   (internal (name-string (if rename? (car (cdr entry)) entry)))
                   (external (name-string (if rename? (car (cdr (cdr entry)))
                                              entry)))
                   (count (definition-count internal defs))
                   (imported-entry (assoc internal imported)))
              (if (assoc external out)
                  (error (string-append "duplicate export in " key ": " external))
                  (cond
                    ((> count 1)
                     (error (string-append "duplicate definition in " key ": "
                                           internal)))
                    ((if (= count 1) (if imported-entry #t #f) #f)
                     (error (string-append "export is both defined and imported in "
                                           key ": " internal)))
                    ((= count 1)
                     (if (string-prefix? "srfi/" key)
                         (error (string-append "SRFI alias defines " internal))
                         (loop (cdr rest)
                               (cons (cons external
                                           (string-append key "#" internal))
                                     out))))
                    (imported-entry
                     (loop (cdr rest)
                           (cons (cons external (cdr imported-entry)) out)))
                    (primitive
                     (loop (cdr rest)
                           (cons (cons external
                                       (string-append "primitive#" internal))
                                 out)))
                    (else
                     (error (string-append "unresolved export in " key ": "
                                           internal)))))))))

    (define (check-libraries root)
      (set! FAILURES '())
      (set! LIBRARY-CACHE '())
      (set! LIBRARY-VISITING '())
      (scan-libraries root)
      (let loop ((entries LIBRARY-FILES))
        (if (null? entries)
            #t
            (begin
              (guard (condition
                      (#t (add-failure
                            (string-append (car (car entries)) ": "
                                           (if (error-object? condition)
                                               (error-object-message condition)
                                               "error")))))
                (resolve-library (car (car entries))))
              (loop (cdr entries)))))
      (report-failures)
      (if (null? FAILURES)
          (begin
            (display "library metadata: PASS (")
            (display (length LIBRARY-FILES))
            (display " libraries, ")
            (display FILE-COUNT)
            (display " files, ")
            (display SCM-COUNT)
            (display " support files)")
            (newline)
            #t)
          (begin
            (display "library metadata: FAILED (")
            (display (length FAILURES))
            (display " findings)")
            (newline)
            #f)))
))

