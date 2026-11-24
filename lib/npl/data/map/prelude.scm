; Shared by the SRFI 146 reference sources: the sample implementations use
; ASSUME and RECEIVE, and the tree matcher uses AND-LET*.
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
