;; Literals and quoted data are immutable (R7RS 3.4): mutating one raises a
;; catchable error, in compiled code and in npli alike. Guile allows the
;; mutation, so the expected output is established from R7RS (oracle=none).
;; Fresh objects, including copies of literals, stay mutable.
(define (try thunk)
  (guard (e ((error-object? e) (error-object-message e))) (thunk) 'mutated))
(define q '("abc" #(1 2) (3 4)))
(define r1 (list (try (lambda () (string-set! (car q) 0 #\z)))
                 (try (lambda () (vector-set! (car (cdr q)) 0 9)))
                 (try (lambda () (set-car! (car (cdr (cdr q))) 9)))
                 (try (lambda () (set-cdr! q '())))))
(define r2 (list (try (lambda () (string-set! "lit" 0 #\z)))
                 (try (lambda () (vector-set! #(1 2) 0 9)))
                 (try (lambda () (vector-fill! #(1 2) 0)))
                 (try (lambda () (bytevector-u8-set! #u8(1) 0 9)))
                 (try (lambda () (string-fill! "ab" #\c)))))
(define r3 (list (try (lambda () (string-set! (string-copy "lit") 0 #\z)))
                 (try (lambda () (vector-set! (vector-copy #(1 2)) 0 9)))
                 (try (lambda () (set-car! (list-copy '(1 2)) 9)))
                 (try (lambda () (string-set! (symbol->string 'sym) 0 #\z)))
                 (try (lambda () (string-set! (make-string 2 #\a) 0 #\z)))))
(display r1) (newline)
(display r2) (newline)
(display r3) (newline)
