(import (scheme write) (except (scheme base) if))
(define (if a b c) (quote proc))
(write (if #t 1 2))
