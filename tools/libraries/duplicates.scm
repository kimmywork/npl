(begin
    ; ---- definition sites and duplicates ---------------------------------
    ; Every top-level definition in lib/ and tools/ .sld files (following
    ; include files by their real path) becomes a site:
    ;
    ;   (name kind source library exported? normalized)
    ;
    ; A shared kernel included by several libraries is one site, because its
    ; source path is the same.  cond-expand alternatives in one file are one
    ; site as well.  The checks over the sites are:
    ;
    ;   1. the same name defined in two source files with the same normalized
    ;      form: define it once and import it (exported or private);
    ;   2. one normalized form (of a non-trivial size) under two different
    ;      names in two source files: a parallel implementation, reuse it;
    ;   3. the same private name defined in two source files with different
    ;      forms: rename one, so helpers do not silently diverge.
    ;
    ; Exported names that reach different definitions are origins.scm's
    ; concern (SRFI/R7RS overlaps need an exception, others a rename).
    (define (site-name site) (car site))
    (define (site-kind site) (car (cdr site)))
    (define (site-source site) (car (cdr (cdr site))))
    (define (site-library site) (car (cdr (cdr (cdr site)))))
    (define (site-exported? site) (car (cdr (cdr (cdr (cdr site))))))
    (define (site-normalized site) (car (cdr (cdr (cdr (cdr (cdr site)))))))

    ; Smallest normalized form (in pairs) that rule 2 compares across names;
    ; below it, one-line wrappers such as (lambda (x) (car x)) would match
    ; unrelated helpers.
    (define IDENTITY-MINIMUM-SIZE 24)

    (define (duplicate-form? form name)
      (if (pair? form)
          (if (symbol? (car form))
              (string=? (symbol->string (car form)) name)
              #f)
          #f))

    (define (duplicate-sld-files directory)
      (if (not (file-exists? directory))
          '()
          (let loop ((names (directory-files directory #t)) (files '()))
            (if (null? names)
                files
                (let ((path (path-join directory (car names))))
                  (if (file-info-directory? (file-info path #f))
                      (loop (cdr names)
                            (append (duplicate-sld-files path) files))
                      (if (extension path ".sld")
                          (loop (cdr names) (cons path files))
                          (loop (cdr names) files))))))))

    (define (duplicate-include-paths paths source context out)
      (if (null? paths)
          out
          (if (string? (car paths))
              (let ((path (path-normalize
                            (path-join (path-directory source) (car paths)))))
                (duplicate-include-paths
                  (cdr paths) source context
                  (duplicate-forms (read-forms path) path context out)))
              (duplicate-include-paths (cdr paths) source context out))))

    ; context: (library-key . exported-names)
    (define (duplicate-add-form form source context out)
      (let loop ((defs (form-defs form source)) (out out))
        (if (null? defs)
            out
            (let* ((name (definition-name (car defs)))
                   (kind (definition-kind (car defs)))
                   (single (if (= (length defs) 1)
                               (identity-definition-form form)
                               #f)))
              (loop (cdr defs)
                    (cons (list name kind source (car context)
                                (if (member name (cdr context)) #t #f)
                                single)
                          out))))))

    (define (duplicate-cond-clauses clauses source context out)
      (if (null? clauses)
          out
          (duplicate-cond-clauses
            (cdr clauses) source context
            (duplicate-forms (cdr (car clauses)) source context out))))

    (define (duplicate-forms forms source context out)
      (if (null? forms)
          out
          (let ((form (car forms)))
            (cond
              ((duplicate-form? form "begin")
               (duplicate-forms (cdr forms) source context
                 (duplicate-forms (cdr form) source context out)))
              ((duplicate-form? form "include")
               (duplicate-forms (cdr forms) source context
                 (duplicate-include-paths (cdr form) source context out)))
              ((duplicate-form? form "include-ci")
               (duplicate-forms (cdr forms) source context
                 (duplicate-include-paths (cdr form) source context out)))
              ((duplicate-form? form "cond-expand")
               (duplicate-forms (cdr forms) source context
                 (duplicate-cond-clauses (cdr form) source context out)))
              (else
               (duplicate-forms (cdr forms) source context
                 (duplicate-add-form form source context out)))))))

    (define (duplicate-library-sites path)
      (let* ((form (library-form (read-forms path)))
             (key (library-name-string (car (cdr form))))
             (exports (map export-internal-name
                           (car (parse-library path))))
             (declarations (cdr (cdr form))))
        (duplicate-forms
          (let loop ((rest declarations) (out '()))
            (if (null? rest)
                (reverse out)
                (loop (cdr rest)
                      (if (if (duplicate-form? (car rest) "export")
                              #t
                              (if (duplicate-form? (car rest) "import")
                                  #t
                                  (duplicate-form? (car rest) "define-library")))
                          out
                          (cons (car rest) out)))))
          path (cons key exports) '())))

    ; One site per (name, source): an included file seen through several
    ; libraries, or alternatives in one file, count once.
    (define (duplicate-site-key site)
      (string-append (site-name site) " @ " (site-source site)))

    (define (duplicate-collect-sites root)
      (let ((seen (make-hash-table string=?)))
        (let loop ((paths (append (duplicate-sld-files (path-join root "lib"))
                                  (duplicate-sld-files (path-join root "tools"))))
                   (sites '()))
          (if (null? paths)
              (reverse sites)
              (loop (cdr paths)
                    (let add ((rest (duplicate-library-sites (car paths)))
                              (sites sites))
                      (if (null? rest)
                          sites
                          (let ((key (duplicate-site-key (car rest))))
                            (if (hash-table-contains? seen key)
                                (add (cdr rest) sites)
                                (begin
                                  (hash-table-set! seen key #t)
                                  (add (cdr rest)
                                       (cons (car rest) sites))))))))))))

    (define (duplicate-group sites key-of)
      (let ((table (make-hash-table string=?)))
        (for-each
          (lambda (site)
            (let ((key (key-of site)))
              (if key
                  (hash-table-set! table key
                                   (cons site (hash-table-ref/default
                                                table key '()))))))
          sites)
        table))

    (define (duplicate-sources sites)
      (let loop ((rest sites) (out '()))
        (if (null? rest)
            (reverse out)
            (loop (cdr rest)
                  (if (member (site-source (car rest)) out)
                      out
                      (cons (site-source (car rest)) out))))))

    (define (duplicate-print-sites title name sites)
      (display title)
      (display name)
      (newline)
      (for-each
        (lambda (site)
          (display "  ")
          (display (site-name site))
          (display (if (site-exported? site) " (exported by " " (private in "))
          (display (site-library site))
          (display ") ")
          (display (site-source site))
          (newline))
        (reverse sites)))

    (define (duplicate-same-form? left right)
      (if (site-normalized left)
          (equal? (site-normalized left) (site-normalized right))
          #f))

    ; Rule 1 and rule 3, by name.
    (define (duplicate-check-names table)
      (let ((failed 0))
        (for-each
          (lambda (name)
            (let ((sites (hash-table-ref table name (lambda () '()))))
              (if (> (length (duplicate-sources sites)) 1)
                  (let ((first (car sites)))
                    (cond
                      ((let same ((rest (cdr sites)))
                         (if (null? rest)
                             #t
                             (if (duplicate-same-form? first (car rest))
                                 (same (cdr rest))
                                 #f)))
                       (set! failed (+ failed 1))
                       (duplicate-print-sites
                         "identical definition (define once, import it): "
                         name sites))
                      ((let private ((rest sites))
                         (if (null? rest)
                             #t
                             (if (site-exported? (car rest))
                                 #f
                                 (private (cdr rest)))))
                       (set! failed (+ failed 1))
                       (duplicate-print-sites
                         "duplicate private definition (rename or share): "
                         name sites))
                      (else #f))))))
          (list-sort-strings (hash-table-keys table)))
        failed))

    ; Rule 2: one normalized form under several names.
    (define (duplicate-check-forms sites)
      (let ((table (duplicate-group
                     sites
                     (lambda (site)
                       (if (site-normalized site)
                           (if (>= (identity-form-size (site-normalized site))
                                   IDENTITY-MINIMUM-SIZE)
                               (identity-form-key (site-normalized site))
                               #f)
                           #f))))
            (failed 0))
        (for-each
          (lambda (key)
            (let* ((group (hash-table-ref table key (lambda () '())))
                   (names (let loop ((rest group) (out '()))
                            (if (null? rest)
                                out
                                (loop (cdr rest)
                                      (if (member (site-name (car rest)) out)
                                          out
                                          (cons (site-name (car rest)) out)))))))
              (if (if (> (length names) 1)
                      (> (length (duplicate-sources group)) 1)
                      #f)
                  (begin
                    (set! failed (+ failed 1))
                    (duplicate-print-sites
                      "same definition under different names (reuse one): "
                      (car (list-sort-strings names)) group)))))
          (list-sort-strings (hash-table-keys table)))
        failed))

    ; Rule 2 also covers forwarders: (define (join d n) (path-join d n)) has
    ; the same signature and effect as path-join under a second name.  The
    ; check is limited to the case that creates a parallel API: a library
    ; outside scheme/ and srfi/ wrapping a procedure that another npl/
    ; library defines.  Accessors over car/cdr/vector, wrappers over C
    ; primitives, SRFI-mandated aliases (mapping-set!, inexact->exact), and
    ; forwarders inside one library are by design.
    (define (duplicate-origin-library origin)
      ; "kind:KEY#NAME" -> KEY; primitives have no library.
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
            #f)))

    (define (duplicate-standard-key? key)
      (if (string-prefix? "srfi/" key)
          #t
          (if (string-prefix? "scheme/" key)
              (not (string-prefix? "scheme/internal/" key))
              #f)))

    ; name -> defining npl/ library, for names whose origin is in npl/.
    (define (duplicate-npl-origins)
      (let ((table (make-hash-table string=?)))
        (for-each
          (lambda (library)
            (for-each
              (lambda (export)
                (let ((key (duplicate-origin-library (cdr export))))
                  (if (if key
                          (if (string-prefix? "npl/" key)
                              (not (string-prefix? "npl/internal/" key))
                              #f)
                          #f)
                      (hash-table-set! table (car export) key))))
              (car (cdr library))))
          LIBRARY-CACHE)
        table))

    ; Names a standard library (srfi/*, scheme/*) exports are mandated by a
    ; specification, so a forwarder under such a name is the spec's alias.
    (define (duplicate-standard-names)
      (let ((table (make-hash-table string=?)))
        (for-each
          (lambda (library)
            (if (duplicate-standard-key? (car library))
                (for-each
                  (lambda (export) (hash-table-set! table (car export) #t))
                  (car (cdr library)))))
          LIBRARY-CACHE)
        table))

    (define (duplicate-check-aliases sites)
      (let ((origins (duplicate-npl-origins))
            (standard (duplicate-standard-names))
            (failed 0))
        (for-each
          (lambda (site)
            (let* ((target (if (site-normalized site)
                               (identity-forward-target (site-normalized site))
                               #f))
                   (target-library
                     (if target
                         (hash-table-ref/default
                           origins (symbol->string target) #f)
                         #f)))
              (if (if target-library
                      (if (not (string=? target-library (site-library site)))
                          (if (duplicate-standard-key? (site-library site))
                              #f
                              (not (hash-table-contains?
                                     standard (site-name site))))
                          #f)
                      #f)
                  (begin
                    (set! failed (+ failed 1))
                    (display "forwarder of ")
                    (display target)
                    (display " from (")
                    (display target-library)
                    (display ") (import it instead): ")
                    (display (site-name site))
                    (display (if (site-exported? site)
                                 " (exported by " " (private in "))
                    (display (site-library site))
                    (display ") ")
                    (display (site-source site))
                    (newline)))))
          sites)
        failed))

    (define (list-sort-strings items)
      ; Insertion sort keeps the report order stable without (npl sort).
      (let loop ((rest items) (out '()))
        (if (null? rest)
            out
            (loop (cdr rest)
                  (let insert ((sorted out))
                    (cond
                      ((null? sorted) (list (car rest)))
                      ((string<? (car rest) (car sorted))
                       (cons (car rest) sorted))
                      (else (cons (car sorted) (insert (cdr sorted))))))))))

    (define (check-duplicate-definitions root)
      (set! LIBRARY-FAILURES '())
      (resolve-all root)
      (let* ((sites (duplicate-collect-sites root))
             (by-name (duplicate-group sites site-name))
             (name-failures (duplicate-check-names by-name))
             (form-failures (+ (duplicate-check-forms sites)
                               (duplicate-check-aliases sites))))
        (if (= (+ name-failures form-failures) 0)
            (begin
              (display "duplicate definitions: PASS (")
              (display (length sites))
              (display " definition sites)")
              (newline)
              #t)
            (begin
              (display "duplicate definitions: FAILED (")
              (display name-failures)
              (display " by name, ")
              (display form-failures)
              (display " by form)")
              (newline)
              #f))))
)
