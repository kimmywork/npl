(define-library (npl frontend)
  (export make-frontend-session
          frontend-expand-datum
          frontend-register-runtime-library!
          frontend-reset-interaction-bindings!
          frontend-require-bound!
          frontend-error?
          frontend-error-message
          frontend-set-standard-root!
          frontend-datum->runtime
          (rename core-expand-letstar frontend-expand-core-letstar)
          (rename core-body-expression frontend-core-body-expression)
          frontend-bytevector-items->runtime
          frontend-set-native-hook!
          frontend-set-c-primitive-hook!
          frontend-numeric-alias-suffixes)
  (import (npl primitive))
  (include "../../src/compiler/common/base.npl")
  (include "../../src/compiler/reader/lexer.npl")
  (include "../../src/compiler/reader/datum.npl")
  (include "../../src/compiler/frontend/derived.npl")
  (include "../../src/compiler/frontend/syntax.npl")
  (include "../../src/compiler/frontend/expand.npl")
  (include "../../src/compiler/frontend/expand-values.npl")
  (include "../../src/compiler/frontend/transcription.npl")
  (include "../../src/compiler/frontend/toplevel.npl")
  (include "../../src/compiler/frontend/forms.npl")
  (include "../../src/compiler/frontend/source.npl")
  (include "../../src/compiler/frontend/primitive-table.npl")
  (include "../../src/compiler/frontend/library-primitives.npl")
  (include "../../src/compiler/frontend/c-primitives.npl")
  (include "../../src/compiler/frontend/c-functions.npl")
  (include "../../src/compiler/frontend/numeric-aliases.npl")
  (include "../../src/compiler/frontend/library.npl")
  (include "../../src/compiler/frontend/library-definitions.npl")
  (begin
    (define FRONTEND-ERROR-TAG 930)
    (define FRONTEND-SESSION-TAG 931)

    (define (frontend-error message)
      (vector FRONTEND-ERROR-TAG message))

    (define (frontend-error? value)
      (if (vector? value)
          (if (> (vector-length value) 0)
              (= (vector-ref value 0) FRONTEND-ERROR-TAG)
              #f)
          #f))

    (define (frontend-error-message value) (vector-ref value 1))

    ; See src/compiler/frontend/library-definitions.npl.
    (define (frontend-set-native-hook! hook)
      (cell-set! LIBRARY-NATIVE-HOOK hook))

    (define (frontend-set-c-primitive-hook! hook)
      (cell-set! C-PRIMITIVE-HOOK hook))

    ; (suffix . operator) for the library procedures behind the public
    ; numeric operators (frontend/numeric-aliases.npl). suffix is the
    ; procedure's emitted name without its library prefix, as keys appear in
    ; %library-definitions entries.
    (define (frontend-numeric-alias-suffixes-in sources)
      (if (null? sources)
          ()
          (cons (cons (sanitize (car (car sources)) 0 "")
                      (car (cdr (car sources))))
                (frontend-numeric-alias-suffixes-in (cdr sources)))))

    (define (frontend-numeric-alias-suffixes)
      (frontend-numeric-alias-suffixes-in PUBLIC-NUMERIC-ALIAS-SOURCES))

    (define (with-frontend-errors thunk)
      (call/cc
        (lambda (return)
          (let ((previous (cell-get FRONTEND-ERROR-HANDLER)))
            (cell-set! FRONTEND-ERROR-HANDLER
              (lambda (message)
                (cell-set! FRONTEND-ERROR-HANDLER previous)
                (return (frontend-error message))))
            (let ((result (thunk)))
              (cell-set! FRONTEND-ERROR-HANDLER previous)
              result)))))

    (define (frontend-capture-state! session)
      (vector-set! session 3 (cell-get DATUM-LABELS))
      (vector-set! session 4 (cell-get ACTIVE-DATUM-LABELS))
      (vector-set! session 5 (cell-get QUASIQUOTE-LEVEL))
      (vector-set! session 6 (cell-get SYNTAX-MARKS))
      (vector-set! session 7 (cell-get SYNTAX-NEXT-MARK))
      (vector-set! session 8 (cell-get CURRENT-PATTERN-VARIABLES))
      (vector-set! session 9 (cell-get CURRENT-REPEAT-DRIVER-DEPTH))
      (vector-set! session 10 (cell-get EXPAND-NEXT-VALUE))
      (vector-set! session 11 (cell-get EXPAND-REQUIRE-BOUND))
      (vector-set! session 12 (cell-get LIBRARY-ROOT))
      (vector-set! session 13 (cell-get LIBRARY-STANDARD-INDEX))
      (vector-set! session 14 (cell-get LIBRARY-REGISTRY))
      (vector-set! session 15 (cell-get LIBRARY-OUTPUT))
      (vector-set! session 16 (cell-get LIBRARY-PRIMITIVE-MACROS))
      (vector-set! session 17 (cell-get LIBRARY-NEXT-ID))
      (vector-set! session 18 (cell-get TRANSCRIPTION-ALIASES))
      (vector-set! session 19 (cell-get TRANSCRIPTION-MACRO-ALIASES))
      session)

    (define (frontend-activate-state! session)
      (cell-set! DATUM-LABELS (vector-ref session 3))
      (cell-set! ACTIVE-DATUM-LABELS (vector-ref session 4))
      (cell-set! QUASIQUOTE-LEVEL (vector-ref session 5))
      (cell-set! SYNTAX-MARKS (vector-ref session 6))
      (cell-set! SYNTAX-NEXT-MARK (vector-ref session 7))
      (cell-set! CURRENT-PATTERN-VARIABLES (vector-ref session 8))
      (cell-set! CURRENT-REPEAT-DRIVER-DEPTH (vector-ref session 9))
      (cell-set! EXPAND-NEXT-VALUE (vector-ref session 10))
      (cell-set! EXPAND-REQUIRE-BOUND (vector-ref session 11))
      (cell-set! LIBRARY-ROOT (vector-ref session 12))
      (cell-set! LIBRARY-STANDARD-INDEX (vector-ref session 13))
      (cell-set! LIBRARY-REGISTRY (vector-ref session 14))
      (cell-set! LIBRARY-OUTPUT (vector-ref session 15))
      (cell-set! LIBRARY-PRIMITIVE-MACROS (vector-ref session 16))
      (cell-set! LIBRARY-NEXT-ID (vector-ref session 17))
      (cell-set! TRANSCRIPTION-ALIASES (vector-ref session 18))
      (cell-set! TRANSCRIPTION-MACRO-ALIASES (vector-ref session 19))
      session)

    (define (frontend-current-state)
      (frontend-capture-state!
        (vector FRONTEND-SESSION-TAG () ()
                () () 0 () 0 () 1000000000 0 0
                "" () () () () 0 () ())))

    (define (frontend-set-standard-root! root)
      (library-set-standard-root! root))

    (define (frontend-reset-library-state)
      (cell-set! LIBRARY-ROOT "")
      (cell-set! LIBRARY-REGISTRY ())
      (cell-set! LIBRARY-STANDARD-INDEX ())
      (cell-set! LIBRARY-OUTPUT ())
      (cell-set! LIBRARY-NEXT-ID 0))

    (define (make-frontend-session)
      (let ((previous (frontend-current-state))
            (session
              (vector FRONTEND-SESSION-TAG () ()
                      () () 0 () 0 () 1000000000 0 0
                      "" () () () () 0 () ())))
        (cell-set! EXPAND-REQUIRE-BOUND 0)
        (cell-set! TRANSCRIPTION-ALIASES ())
        (cell-set! TRANSCRIPTION-MACRO-ALIASES ())
        (cell-set! SYNTAX-MARKS ())
        (cell-set! SYNTAX-NEXT-MARK 0)
        (cell-set! EXPAND-NEXT-VALUE 0)
        (frontend-reset-library-state)
        (let ((derived
                (expand-top-sequence
                  (parse-source-text DERIVED-SYNTAX-SOURCE) () () ())))
          (if (not (null? (car derived)))
              (fatal "derived syntax source emitted runtime forms"))
          (cell-set! LIBRARY-PRIMITIVE-MACROS (cdr derived))
          (vector-set! session 2 (cdr derived)))
        (frontend-capture-state! session)
        (frontend-activate-state! previous)
        session))

    (define (frontend-runtime-bindings exports)
      (if (null? exports)
          ()
          (cons
            (library-binding (car (car exports)) 1 (cdr (car exports)))
            (frontend-runtime-bindings (cdr exports)))))

    (define (frontend-register-runtime-library! session key exports)
      (let ((previous (frontend-current-state)))
        (frontend-activate-state! session)
        (let ((entry (library-entry key () "")))
          (cell-set! (library-entry-state entry) 2)
          (cell-set! (library-entry-exports entry)
                     (frontend-runtime-bindings exports))
          (cell-set! LIBRARY-REGISTRY
                     (cons entry (cell-get LIBRARY-REGISTRY))))
        (frontend-capture-state! session)
        (frontend-activate-state! previous)
        session))

    (define (frontend-require-bound! session)
      (vector-set! session 11 2)
      session)

    (define (frontend-reset-interaction-bindings! session)
      (vector-set! session 1 '())
      (vector-set! session 2 '())
      session)

    (define (frontend-session-value-environment session)
      (vector-ref session 1))

    (define (frontend-session-macro-environment session)
      (vector-ref session 2))

    (define (frontend-session-set-value-environment! session environment)
      (vector-set! session 1 environment))

    (define (frontend-session-set-macro-environment! session environment)
      (vector-set! session 2 environment))

    (define (frontend-datum-source datum)
      (let ((port (open-output-string)))
        (write datum port)
        (get-output-string port)))

    (define (frontend-import-form session form)
      (cell-set! LIBRARY-OUTPUT ())
      (let ((bundle
              (library-import-sets
                (cdr form)
                (cons (frontend-session-value-environment session)
                      (cons
                        (frontend-session-macro-environment session) ())))))
        (frontend-session-set-value-environment! session (car bundle))
        (frontend-session-set-macro-environment!
          session (car (cdr bundle)))
        (lower-program (cell-get LIBRARY-OUTPUT))))

    (define (frontend-expand-form session form)
      (if (raw-form? form "import")
          (frontend-import-form session form)
          (let ((value-environment
                  (predeclare-global-identifiers
                    (direct-defined-identifiers form)
                    (frontend-session-value-environment session))))
            (let ((expanded
                    (expand-top-sequence
                      (cons form ()) value-environment
                      (frontend-session-macro-environment session) ())))
              ; The final value environment includes names defined by
              ; spliced begin forms and macro output.
              (frontend-session-set-value-environment!
                session (cell-get TOP-VALUE-ENVIRONMENT))
              (frontend-session-set-macro-environment!
                session (cdr expanded))
              (lower-program (car expanded))))))

    (define (frontend-runtime-list values)
      (if (null? values)
          ()
          (cons (frontend-datum->runtime (car values))
                (frontend-runtime-list (cdr values)))))

    (define (frontend-runtime-symbol name)
      (string->symbol name))

    (define (frontend-write-bytevector-items items port)
      (if (null? items)
          (get-output-bytevector port)
          (begin
            (write-u8 (car items) port)
            (frontend-write-bytevector-items (cdr items) port))))

    (define (frontend-runtime-bytevector items)
      (frontend-write-bytevector-items items (open-output-bytevector)))

    (define (frontend-bytevector-items->runtime items)
      (frontend-runtime-bytevector items))

    (define (frontend-datum->runtime datum)
      (cond
        ((number? datum) datum)
        ((null? datum) ())
        ((datum-symbol? datum)
         (frontend-runtime-symbol (datum-symbol-name datum)))
        ; A copy: the evaluator marks constants immutable
        ; (evaluator-compile.scm eval-literal), and the datum's text belongs
        ; to the frontend.
        ((datum-string? datum)
         (let ((text (datum-string-text datum)))
           (substring text 0 (string-length text))))
        ((datum-char? datum) (integer->char (datum-char-code datum)))
        ((datum-vector? datum)
         (list->vector
           (frontend-runtime-list (datum-vector-items datum))))
        ((datum-bytevector? datum)
         (frontend-runtime-bytevector (datum-bytevector-items datum)))
        ((datum-true? datum) #t)
        ((datum-false? datum) #f)
        ((pair? datum)
         (cons (frontend-datum->runtime (car datum))
               (frontend-datum->runtime (cdr datum))))
        (else (fatal "cannot convert frontend datum"))))

    (define (frontend-expand-datum session datum)
      (let ((previous (frontend-current-state)))
        (frontend-activate-state! session)
        (let ((result
                (with-frontend-errors
                  (lambda ()
                    (let ((forms
                            (parse-source-text (frontend-datum-source datum))))
                      (if (not (= (raw-list-count forms 0) 1))
                          (fatal "interaction must contain one datum"))
                      (frontend-expand-form session (car forms)))))))
          (frontend-capture-state! session)
          (frontend-activate-state! previous)
          result)))))
