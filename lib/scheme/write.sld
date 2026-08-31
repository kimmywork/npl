(define-library (scheme write)
  (export display write write-shared write-simple)
  (import (only (npl primitive)
                display write write-shared write-simple)))
