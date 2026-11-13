; Shared by the SRFI 146 reference sources: the sample implementations use
; ASSUME and RECEIVE, and the tree matcher uses AND-LET*.
(define (assume expression . message)
  (if expression
      #t
      (if (null? message)
          (error "assumption failed")
          (apply error (car message) (cdr message)))))

(define-syntax receive
  (syntax-rules ()
    ((_ formals expression body ...)
     (call-with-values (lambda () expression)
                       (lambda formals body ...)))))

(define-syntax and-let*
  (syntax-rules ()
    ((_ () body ...) (begin body ...))
    ((_ ((variable expression) clause ...) body ...)
     (let ((variable expression))
       (and variable (and-let* (clause ...) body ...))))
    ((_ ((expression) clause ...) body ...)
     (and expression (and-let* (clause ...) body ...)))
    ((_ (variable clause ...) body ...)
     (and variable (and-let* (clause ...) body ...)))))
