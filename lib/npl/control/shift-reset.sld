;; SRFI 226 shift/reset component (T4-B prototype subset).
(define-library (npl control shift-reset)
  (export reset reset-at shift shift-at)
  (import (npl control)))
