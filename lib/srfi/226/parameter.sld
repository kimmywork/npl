;; SRFI 226 parameter component alias (singular library name per erratum).
(define-library (srfi 226 parameter)
  (export make-parameter parameterize parameter?
          current-parameterization parameterization?
          call-with-parameterization temporarily)
  (import (npl control parameters)))
