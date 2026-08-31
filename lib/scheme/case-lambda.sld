(define-library (scheme case-lambda)
  (export case-lambda)
  (import (only (scheme base) car cdr null? pair? raise))
  (begin
    (define-syntax case-lambda-bind-fixed
      (syntax-rules ()
        ((_ () arguments body ...)
         (begin body ...))
        ((_ (name more ...) arguments body ...)
         (let ((name (car arguments)))
           (case-lambda-bind-fixed
             (more ...) (cdr arguments) body ...)))))

    (define-syntax case-lambda-bind-dotted
      (syntax-rules ()
        ((_ () rest arguments body ...)
         ((lambda (rest) body ...) arguments))
        ((_ (name more ...) rest arguments body ...)
         (let ((name (car arguments)))
           (case-lambda-bind-dotted
             (more ...) rest (cdr arguments) body ...)))))

    (define-syntax case-lambda-exact
      (syntax-rules ()
        ((_ () arguments yes no)
         (if (null? arguments) yes no))
        ((_ (name more ...) arguments yes no)
         (if (pair? arguments)
             (case-lambda-exact (more ...) (cdr arguments) yes no)
             no))))

    (define-syntax case-lambda-at-least
      (syntax-rules ()
        ((_ () arguments yes no) yes)
        ((_ (name more ...) arguments yes no)
         (if (pair? arguments)
             (case-lambda-at-least (more ...) (cdr arguments) yes no)
             no))))

    (define-syntax case-lambda-dispatch
      (syntax-rules ()
        ((_ arguments)
         (raise "case-lambda: no matching clause"))
        ((_ arguments (() body ...) more ...)
         (if (null? arguments)
             (begin body ...)
             (case-lambda-dispatch arguments more ...)))
        ((_ arguments ((name more-names ...) body ...) more ...)
         (case-lambda-exact
           (name more-names ...) arguments
           (case-lambda-bind-fixed
             (name more-names ...) arguments body ...)
           (case-lambda-dispatch arguments more ...)))

        ((_ arguments ((name more-names ... . rest) body ...) more ...)
         (case-lambda-at-least
           (name more-names ...) arguments
           (case-lambda-bind-dotted
             (name more-names ...) rest arguments body ...)
           (case-lambda-dispatch arguments more ...)))
        ((_ arguments (rest body ...) more ...)
         ((lambda (rest) body ...) arguments))))

    (define-syntax case-lambda
      (syntax-rules ()
        ((_ clause ...)
         (lambda arguments
           (case-lambda-dispatch arguments clause ...)))))))
