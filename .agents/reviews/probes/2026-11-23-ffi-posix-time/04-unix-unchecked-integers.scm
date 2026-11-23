; R3: numeric arguments reach npl_fixnum_value without a type check.
; Expected after the fix: every line is (error ...); today the calls succeed
; and the file or directory gets a mode decoded from the symbol/string bits.
(import (scheme base) (scheme write) (scheme file) (npl process) (npl unix))
(define root (make-temporary-directory))
(define file (string-append root "/f"))
(call-with-output-file file (lambda (port) (write-string "x" port)))
(define (try name thunk)
  (write (list name
               (guard (e (#t (list 'error (error-object-message e))))
                 (thunk)
                 'accepted)))
  (newline))
(try 'truncate-file (lambda () (truncate-file file "0")))
(try 'create-directory
     (lambda () (create-directory (string-append root "/d") "rwx")))
(try 'set-file-mode (lambda () (set-file-mode file 'abc)))
(try 'set-umask! (lambda () (set-umask! 'all)))
(set-umask! #o022)
(set-file-mode file #o600)
(if (file-exists? (string-append root "/d"))
    (begin (set-file-mode (string-append root "/d") #o700)
           (delete-directory (string-append root "/d"))))
(delete-file file)
(delete-directory root)
