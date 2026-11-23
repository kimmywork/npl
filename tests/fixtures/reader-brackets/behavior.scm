; Square brackets are list delimiters, but they must pair with brackets:
; (a b] and [a b) are read errors (R7RS 2.4 reads lists the same way).
(define (report name value) (write (list name value)) (newline))
(define (rd text) (read (open-input-string text)))
(define (caught thunk)
  (guard (e ((read-error? e) 'read-error)) (thunk) 'not-caught))

(report 'valid-bracket (rd "[a b]"))
(report 'nested-bracket (rd "[a (b c) [d]]"))
(report 'dotted-bracket (rd "[a . b]"))
(report 'errors
        (map (lambda (text) (caught (lambda () (rd text))))
             (list "(a b]" "[a b)" "]" "[a . b)")))
