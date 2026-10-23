; The evaluator behind (scheme eval) and (scheme load) in compiled programs.
; npli registers its own (npl eval-runtime) bound to the running evaluator
; (EVAL-RUNTIME-LIBRARY-EXPORTS in lib/npl/evaluator-session.scm).
(define-library (npl eval-runtime)
  (export (rename make-eval-environment-from-imports make-environment)
          (rename eval-in-environment evaluate)
          load-file)
  (import (npl evaluator))
  (begin
    (define load-environment (make-eval-environment))

    (define (load-file filename)
      (eval-load-file filename load-environment))))
