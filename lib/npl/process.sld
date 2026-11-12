(define-library (npl process)
  (export delete-directory executable-path make-temporary-directory
          run-process set-command-line!)
  (import (only (npl internal primitive)
                delete-directory executable-path make-temporary-directory
                run-process set-command-line!)))
