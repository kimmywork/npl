(define-library (scheme file)
  (export call-with-input-file call-with-output-file
          delete-file file-exists?
          open-input-file open-output-file
          open-binary-input-file open-binary-output-file
          with-input-from-file with-output-to-file)
  (import (scheme base)
          (only (npl primitive)
                delete-file file-exists? npl_try_open_file npl_make_record
                npl_list_to_values))
  (begin
    ; Port kinds for npl_try_open_file; a refused open is a file error
    ; (error object layout: lib/scheme/error.scm).
    (define (open-file filename kind who)
      (if (not (string? filename))
          (error (string-append who ": expected a string") filename))
      (let ((port (npl_try_open_file filename kind)))
        (if port
            port
            (raise (npl_make_record 'npl-error-object 'file
                     (string-append who ": cannot open file")
                     (list filename))))))
    (define (open-input-file filename)
      (open-file filename 0 "open-input-file"))
    (define (open-output-file filename)
      (open-file filename 1 "open-output-file"))
    (define (open-binary-input-file filename)
      (open-file filename 2 "open-binary-input-file"))
    (define (open-binary-output-file filename)
      (open-file filename 3 "open-binary-output-file"))
    (define (call-with-input-file filename proc)
      (call-with-port (open-input-file filename) proc))
    (define (call-with-output-file filename proc)
      (call-with-port (open-output-file filename) proc))
    ; The port is the current port for the dynamic extent of thunk and is
    ; closed when thunk returns (R7RS 6.13.1).
    (define (with-input-from-file filename thunk)
      (let ((port (open-input-file filename)))
        (call-with-values
          (lambda () (parameterize ((current-input-port port)) (thunk)))
          (lambda results (close-port port) (npl_list_to_values results)))))
    (define (with-output-to-file filename thunk)
      (let ((port (open-output-file filename)))
        (call-with-values
          (lambda () (parameterize ((current-output-port port)) (thunk)))
          (lambda results (close-port port) (npl_list_to_values results)))))))
