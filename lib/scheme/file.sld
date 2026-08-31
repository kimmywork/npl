(define-library (scheme file)
  (export open-input-file open-output-file
          open-binary-input-file open-binary-output-file)
  (import (only (npl primitive)
                open-input-file open-output-file
                open-binary-input-file open-binary-output-file)))
