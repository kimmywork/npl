;; SRFI 226 continuation inspection component.
(define-library (npl control inspection)
  (export continuation? non-composable-continuation?)
  (import (npl control)))
