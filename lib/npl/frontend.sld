(define-library (npl frontend)
  (export make-frontend-session
          frontend-expand-datum
          frontend-error?
          frontend-error-message
          frontend-set-standard-root!
          frontend-datum->runtime
          frontend-bytevector-items->runtime)
  (import (npl primitive))
  (include "../../src/compiler/common/base.npl")
  (include "../../src/compiler/reader/lexer.npl")
  (include "../../src/compiler/reader/datum.npl")
  (include "../../src/compiler/frontend/derived.npl")
  (include "../../src/compiler/frontend/syntax.npl")
  (include "../../src/compiler/frontend/expand.npl")
  (include "../../src/compiler/frontend/forms.npl")
  (include "../../src/compiler/frontend/source.npl")
  (include "../../src/compiler/frontend/library.npl")
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

    (define (frontend-set-standard-root! root)
      (library-set-standard-root! root))

    (define (frontend-reset-library-state)
      (cell-set! LIBRARY-ROOT "")
      (cell-set! LIBRARY-REGISTRY ())
      (cell-set! LIBRARY-STANDARD-INDEX ())
      (cell-set! LIBRARY-OUTPUT ())
      (cell-set! LIBRARY-NEXT-ID 0))

    (define (make-frontend-session)
      (cell-set! EXPAND-REQUIRE-BOUND 0)
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
        (vector FRONTEND-SESSION-TAG () (cdr derived))))

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
              (frontend-session-set-value-environment!
                session value-environment)
              (frontend-session-set-macro-environment!
                session (cdr expanded))
              (lower-program (car expanded))))))

    (define (frontend-runtime-list values)
      (if (null? values)
          ()
          (cons (frontend-datum->runtime (car values))
                (frontend-runtime-list (cdr values)))))

    (define (frontend-runtime-symbol name)
      (read (open-input-string name)))

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
        ((datum-string? datum) (datum-string-text datum))
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
      (with-frontend-errors
        (lambda ()
          (let ((forms
                  (parse-source-text (frontend-datum-source datum))))
            (if (not (= (raw-list-count forms 0) 1))
                (fatal "interaction must contain one datum"))
            (frontend-expand-form session (car forms))))))))
