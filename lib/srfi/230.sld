;; (srfi 230) is a re-export-only alias for (npl atomic), following the
;; convention documented in docs/reference/README.md.
(define-library (srfi 230)
  (export memory-order memory-order?
          relaxed acquire release acquire-release sequentially-consistent
          make-atomic-flag atomic-flag? atomic-flag-test-and-set!
          atomic-flag-clear!
          make-atomic-box atomic-box? atomic-box-ref atomic-box-set!
          atomic-box-swap! atomic-box-compare-and-swap!
          make-atomic-fxbox atomic-fxbox? atomic-fxbox-ref atomic-fxbox-set!
          atomic-fxbox-swap! atomic-fxbox-compare-and-swap!
          atomic-fxbox+/fetch! atomic-fxbox-/fetch!
          atomic-fxbox-and/fetch! atomic-fxbox-ior/fetch!
          atomic-fxbox-xor/fetch!
          make-atomic-pair atomic-pair? atomic-pair-ref atomic-pair-set!
          atomic-pair-swap! atomic-pair-compare-and-swap!
          atomic-fence)
  (import (npl atomic)))
