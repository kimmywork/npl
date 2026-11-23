; External native package sample (deployment mode B).
;
; The shared object is built by the normal CMake flow and found through
; (npl native); no primitive row, runtime source, or seed change is needed.
; crc32 reads the bytevector directly and returns a fixnum.
(define-library (npl demo crc32)
  (export crc32)
  (import (scheme base)
          (npl ffi)
          (npl native))
  (begin
    (load-native-library "npl_demo_crc32")

    (define crc32-raw
      (native-procedure "npl_demo_crc32" 1))

    (define (crc32 bytes)
      (if (bytevector? bytes)
          (crc32-raw bytes)
          (error "crc32: expected a bytevector" bytes)))))
