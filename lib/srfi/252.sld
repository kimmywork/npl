; SRFI 252 compatibility alias for (npl test property).
(define-library (srfi 252)
  (export test-property test-property-expect-fail test-property-skip
          test-property-error test-property-error-type
          property-test-runner property-seed
          boolean-generator bytevector-generator char-generator
          exact-integer-generator integer-generator
          exact-real-generator real-generator number-generator
          rational-generator string-generator symbol-generator
          complex-generator)
  (import (npl test property)))
