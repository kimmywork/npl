;; SRFI 226 shift/reset component alias.
(define-library (srfi 226 shift-reset)
  (export reset reset-at shift shift-at)
  (import (npl control shift-reset)))
