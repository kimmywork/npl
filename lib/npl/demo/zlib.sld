; External native package sample (deployment mode B) over system zlib.
;
; The shared object is loaded through (npl native).  Compress allocates a
; zlib-bound output buffer; uncompress takes the expected output size so the
; C side never allocates.
(define-library (npl demo zlib)
  (export zlib-version zlib-compress zlib-uncompress
          zlib-compress-bound zlib-available?)
  (import (scheme base)
          (npl ffi)
          (npl native))
  (begin
    (define loaded? #f)
    (define version-raw #f)
    (define bound-raw #f)
    (define compress-raw #f)
    (define uncompress-raw #f)

    (define (zlib-available?)
      (if loaded?
          #t
          (let ((path (native-library-path "npl_demo_zlib")))
            (if (not path)
                #f
                (let ((loaded (ffi-load-library path)))
                  (if (string? loaded)
                      #f
                      (begin
                        (set! version-raw
                              (ffi-procedure "npl_demo_zlib_version" 1))
                        (set! bound-raw
                              (ffi-procedure "npl_demo_zlib_bound" 1))
                        (set! compress-raw
                              (ffi-procedure "npl_demo_zlib_compress" 2))
                        (set! uncompress-raw
                              (ffi-procedure "npl_demo_zlib_uncompress" 2))
                        (set! loaded?
                              (if version-raw
                                  (if bound-raw
                                      (if compress-raw
                                          (if uncompress-raw #t #f)
                                          #f)
                                      #f)
                                  #f))
                        loaded?)))))))

    (define (require-loaded)
      (if (not (zlib-available?))
          (error "zlib package not built or not found")))

    (define (zlib-version)
      (require-loaded)
      (let ((buffer (make-bytevector 32)))
        (let ((length (version-raw buffer)))
          (if (< length 0)
              (error "zlib-version failed" length)
              (utf8->string (bytevector-copy buffer 0 length))))))

    (define (zlib-compress-bound length)
      (require-loaded)
      (let ((bound (bound-raw length)))
        (if (< bound 0)
            (error "zlib-compress-bound failed" length)
            bound)))

    (define (zlib-compress bytes)
      (require-loaded)
      (if (not (bytevector? bytes))
          (error "zlib-compress: expected a bytevector" bytes))
      (let* ((bound (zlib-compress-bound (bytevector-length bytes)))
             (destination (make-bytevector (if (= bound 0) 1 bound)))
             (length (compress-raw bytes destination)))
        (if (< length 0)
            (error "zlib-compress failed" length)
            (bytevector-copy destination 0 length))))

    (define (zlib-uncompress bytes size)
      (require-loaded)
      (if (not (bytevector? bytes))
          (error "zlib-uncompress: expected a bytevector" bytes))
      (if (not (if (exact-integer? size) (>= size 0) #f))
          (error "zlib-uncompress: expected a non-negative size" size))
      (let ((destination (make-bytevector (if (= size 0) 1 size))))
        (let ((length (uncompress-raw bytes destination)))
          (if (< length 0)
              (error "zlib-uncompress failed" length)
              (bytevector-copy destination 0 length)))))))
