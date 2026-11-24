; R21: set-file-times checks the nanosecond range but not the seconds, and
; open-file checks only that flags is an integer.  A bignum reaches
; npl_fixnum_value / the C list branch.
; Expected: both lines are (error ...) naming the argument.
(import (scheme base) (scheme write) (scheme file)
        (only (npl process) make-temporary-directory)
        (npl unix) (npl time))
(define root (make-temporary-directory))
(define file (string-append root "/f"))
(call-with-output-file file (lambda (port) (write-string "x" port)))
(define (try name thunk)
  (write (list name (guard (e (#t (list 'error (error-object-message e))))
                      (thunk))))
  (newline))
(try 'set-file-times-2^70
     (lambda ()
       (let ((t (make-time time-utc 0 (expt 2 70))))
         (set-file-times file t t)
         (list 'mtime (time-second (file-info:mtime (file-info file #t)))))))
(try 'open-file-flags-2^70
     (lambda ()
       (close-port (open-file file binary-output (expt 2 70)))
       'opened))
(delete-file file)
(delete-directory root)
