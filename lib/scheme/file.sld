(define-library (scheme file)
  (export delete-file file-exists?
          open-input-file open-output-file
          open-binary-input-file open-binary-output-file)
  (import (only (npl primitive)
                delete-file file-exists?
                open-input-file open-output-file
                open-binary-input-file open-binary-output-file)))
