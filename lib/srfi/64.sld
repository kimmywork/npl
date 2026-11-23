; SRFI 64 compatibility alias for (npl test).
(define-library (srfi 64)
  (export test-begin test-end test-assert test-equal test-eqv test-eq
          test-inexact test-error test-expect-fail test-skip test-group
          test-group-with-cleanup test-identity finish-tests
          test-runner? test-runner-current test-runner-factory
          test-runner-reset test-runner-pass-count test-runner-fail-count
          test-runner-skip-count test-runner-xfail-count
          test-runner-total-count test-runner-result
          test-record! test-record-skip! test-runner-simple test-runner-null
          %test-begin %test-end %test-assert %test-equal %test-eqv
          %test-eq %test-inexact %test-error %test-group
          test-expect-fail! test-skip!)
  (import (npl test)))
