; Shared RECEIVE macro for the SRFI list/map reference sources.
(define-syntax receive
  (syntax-rules ()
    ((_ formals expression body ...)
     (call-with-values (lambda () expression)
                       (lambda formals body ...)))))
