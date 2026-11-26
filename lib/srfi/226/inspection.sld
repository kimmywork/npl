;; SRFI 226 continuation inspection component alias.
(define-library (srfi 226 inspection)
  (export continuation? non-composable-continuation?)
  (import (npl control inspection)))
