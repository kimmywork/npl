;; SRFI 226 continuation component (T4-A/T4-B prototype subset).
(define-library (npl control continuations)
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
  (import (npl control)))
