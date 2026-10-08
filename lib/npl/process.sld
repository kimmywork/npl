(define-library (npl process)
  (export delete-directory executable-path make-temporary-directory
          run-process set-command-line!)
  (import (only (npl primitive)
                delete-directory executable-path make-temporary-directory
                run-process set-command-line!)))
