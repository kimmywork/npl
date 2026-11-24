(define-library (npl process)
  (export delete-directory executable-path make-temporary-directory
          run-process run-process* set-command-line!)
  (import (only (npl internal primitive)
                delete-directory executable-path make-temporary-directory
                run-process set-command-line!))
  (begin
    (define-c-primitive (npl_process_run_full
                          (arguments any) (input any) (output any)
                          (error any)) any
      "npl_process_run_full")
    (define (run-process* arguments input output error)
      (npl_process_run_full arguments input output error))))
