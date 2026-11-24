; Target platform identity.
;
; cond-expand runs before library loading, so its feature source is a
; bootstrap hook in the runtime, not a package.  This library is the thin,
; user-facing NPL wrapper over that hook; it adds no C of its own.
(define-library (npl platform)
  (export platform-triple platform-features platform-feature?)
  (import (scheme base)
          (only (npl internal primitive)
                npl_platform_triple npl_platform_features
                npl_platform_feature))
  (begin
    (define (platform-triple)
      (npl_platform_triple))

    (define (platform-features)
      (npl_platform_features))

    (define (platform-feature? name)
      (if (symbol? name)
          (npl_platform_feature (symbol->string name))
          (npl_platform_feature name)))))
