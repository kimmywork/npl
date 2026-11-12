(define-library (scheme write)
  (export display write write-shared write-simple)
  (import (only (npl internal primitive)
                display write write-shared write-simple)))
