(import (scheme base) (scheme write))
(define-syntax make-definer
  (syntax-rules ()
    ((_ definer body ...)
     (let ()
       (define-syntax definer (syntax-rules () ((_ n) (define (n) 'v))))
       body ...))))
(write (make-definer def (def get) (get)))
