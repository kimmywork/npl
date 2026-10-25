(define-library (scheme process-context)
  (export command-line exit emergency-exit
          get-environment-variable get-environment-variables)
  (import
    (rename
      (only (npl primitive)
            command-line exit
            get-environment-variable get-environment-variables car null?
            npl_wind_exit_step)
      (exit process-exit)))
  (begin
    ; exit runs the after thunk of every dynamic-wind still in effect,
    ; innermost first, each outside its own extent (R7RS 6.14);
    ; emergency-exit runs none.
    (define (exit-unwind)
      (let ((after (npl_wind_exit_step)))
        (if after
            (begin (after) (exit-unwind))
            #t)))

    (define (exit . code)
      (exit-unwind)
      (if (null? code) (process-exit) (process-exit (car code))))

    (define (emergency-exit . code)
      (if (null? code) (process-exit) (process-exit (car code))))))
