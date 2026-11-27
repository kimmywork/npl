;; SRFI 226 continuation component alias (singular library name per erratum).
(define-library (srfi 226 continuation)
  (export &continuation
          make-continuation-violation continuation-violation?
          continuation-violation-prompt-tag
          call-with-non-composable-continuation
          call-with-current-continuation call/cc
          call-with-composable-continuation
          call-in-continuation call-in return-to
          call-with-continuation-barrier
          continuation-prompt-available?
          dynamic-wind unwind-protect)
  (import (npl control continuations)))
