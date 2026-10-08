(import (scheme base) (scheme write) (scheme process-context))
; Shared by npli script mode, nplc, and Guile: argument passing, definitions
; after expressions, and the (exit) status contract.
(define (report name value) (write (list name value)) (newline))
(report 'arguments (cdr (command-line)))
(define (fact n) (if (= n 0) 1 (* n (fact (- n 1)))))
(report 'fact (fact 20))
(report 'env (get-environment-variable "NPLI_SCRIPT_CHECK"))
(exit (if (null? (cdr (command-line))) #t (string->number (car (cdr (command-line))))))
