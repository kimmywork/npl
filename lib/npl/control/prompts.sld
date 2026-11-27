;; SRFI 226 prompt component (T4-A/T4-B prototype subset).
(define-library (npl control prompts)
  (export &continuation
          make-continuation-violation continuation-violation?
          continuation-violation-prompt-tag
          make-continuation-prompt-tag default-continuation-prompt-tag
          continuation-prompt-tag?
          call-with-continuation-prompt abort-current-continuation)
  (import (npl control)))
