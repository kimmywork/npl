; R9: [ and ] are aliases of ( and ) without pairing; (1 2] and [a b) read.
; Guile and Racket reject a mismatched pair.
(import (scheme base) (scheme write) (scheme read))
(write (guard (e (#t (list 'error (error-object-message e))))
         (read (open-input-string "(1 2]"))))
(newline)
(write (guard (e (#t (list 'error (error-object-message e))))
         (read (open-input-string "[a b)"))))
(newline)
(write (guard (e (#t (list 'error (error-object-message e))))
         (read (open-input-string "]"))))
(newline)
