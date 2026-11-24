(define-syntax probe
  (syntax-rules (=>)
    ((_ arg ... => body ...) '((arg ...) (body ...)))))
(write (probe a => b => c)) (newline)
