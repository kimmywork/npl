; External native package sample.
;
; The package declares one native library.  nplc links its archive in
; --native=static mode and its shared object in --native=shared mode; npli
; loads the shared object through (npl native).  The C implementation uses
; the NplVal ABI inline accessors and links no runtime.
(define-library (npl demo crc32)
  (export crc32)
  (import (scheme base)
          (npl ffi)
          (npl native))
  (begin
    (native-package "npl_demo_crc32"))
  (cond-expand
    (npl-native-linked
     (begin
       (define-c-primitive (crc32-raw (bytes bytevector)) fixnum
         "npl_demo_crc32")))
    (else
     (begin
       (load-native-library "npl_demo_crc32")
       (define crc32-raw
         (native-procedure "npl_demo_crc32" 1)))))
  (begin
    (define (crc32 bytes)
      (if (bytevector? bytes)
          (crc32-raw bytes)
          (error "crc32: expected a bytevector" bytes)))))
