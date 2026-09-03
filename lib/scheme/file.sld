(define-library (scheme file)
  (export call-with-input-file call-with-output-file
          delete-file file-exists?
          open-input-file open-output-file
          open-binary-input-file open-binary-output-file)
  (import (only (scheme base) call-with-port)
          (only (npl primitive)
                delete-file file-exists?
                open-input-file open-output-file
                open-binary-input-file open-binary-output-file))
  (begin
    (define (call-with-input-file filename proc)
      (call-with-port (open-input-file filename) proc))
    (define (call-with-output-file filename proc)
      (call-with-port (open-output-file filename) proc))))
