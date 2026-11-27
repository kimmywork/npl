;; SRFI 226 prompt component alias (singular library name per erratum).
(define-library (srfi 226 prompt)
  (export &continuation
          make-continuation-violation continuation-violation?
          continuation-violation-prompt-tag
          make-continuation-prompt-tag default-continuation-prompt-tag
          continuation-prompt-tag?
          call-with-continuation-prompt abort-current-continuation)
  (import (npl control prompts)))
