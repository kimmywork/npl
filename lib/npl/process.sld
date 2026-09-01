(define-library (npl process)
  (export delete-directory make-temporary-directory run-process)
  (import (only (npl primitive)
                delete-directory make-temporary-directory run-process)))
