; SRFI 88 keyword objects, canonical NPL library.
;
; The reader accepts the SRFI 88 spelling foo: and the Guile-compatible
; spelling #:foo for the same interned keyword object.
(define-library (npl keyword)
  (export keyword? keyword->string string->keyword)
  (import (scheme base)
          (only (npl internal primitive)
                keyword? keyword->string string->keyword)))
