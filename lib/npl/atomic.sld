;; (npl atomic) — SRFI 230 atomic flags, boxes, fixnum boxes and pairs.
;;
;; Optional memory-order arguments and the memory-order syntax stay NPL
;; policy; runtime/atomic.c supplies the indivisible operations and their GC
;; integration.  The T3 baseline serializes every operation, including
;; fences, through one atomic-domain mutex.  It deliberately provides
;; sequentially-consistent behavior for every requested order; it is not a
;; lock-free implementation.
;;
;; Fixnum boxes reject a result outside the signed 62-bit fixnum range before
;; changing the box.  SRFI 230 does not specify overflow, so this is NPL's
;; documented policy rather than a wrapping guarantee.
(define-library (npl atomic)
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
  (import (scheme base))
  (begin
    ;; memory-order checks the identifier's NAME, independently of any
    ;; lexical binding, and expands to the corresponding ordinary symbol.
    ;; A procedural transformer is required: syntax-rules literal matching
    ;; is binding-sensitive and would reject a shadowed `acquire`.
    (define-syntax memory-order
      (lambda (form)
        (define (identifier-name identifier)
          (let ((datum (syntax->datum identifier)))
            (if (symbol? datum) (symbol->string datum) #f)))
        (syntax-case form ()
          ((_)
           (syntax
             (syntax-error "memory-order: expected one identifier")))
          ((_ token)
           (let ((name (identifier-name (syntax token))))
             (cond
               ((and name (string=? name "relaxed"))
                (syntax (quote relaxed)))
               ((and name (string=? name "acquire"))
                (syntax (quote acquire)))
               ((and name (string=? name "release"))
                (syntax (quote release)))
               ((and name (string=? name "acquire-release"))
                (syntax (quote acquire-release)))
               ((and name (string=? name "sequentially-consistent"))
                (syntax (quote sequentially-consistent)))
               (else
                (syntax
                  (syntax-error "memory-order: invalid memory order"
                                token))))))
          ((_ token extra ...)
           (syntax
             (syntax-error "memory-order: expected one identifier"))))))

    (define relaxed 'relaxed)
    (define acquire 'acquire)
    (define release 'release)
    (define acquire-release 'acquire-release)
    (define sequentially-consistent 'sequentially-consistent)

    (define (memory-order? object)
      (cond
        ((eq? object 'relaxed) #t)
        ((eq? object 'acquire) #t)
        ((eq? object 'release) #t)
        ((eq? object 'acquire-release) #t)
        ((eq? object 'sequentially-consistent) #t)
        (else #f)))

    ;; The C substrate receives a validated small integer.  All five orders
    ;; currently select the same locked implementation.
    (define (atomic-order-code order who)
      (cond
        ((eq? order 'relaxed) 0)
        ((eq? order 'acquire) 1)
        ((eq? order 'release) 2)
        ((eq? order 'acquire-release) 3)
        ((eq? order 'sequentially-consistent) 4)
        (else (error who "expected a memory order" order))))

    (define (atomic-optional-order rest who)
      (cond
        ((null? rest) 4)
        ((null? (cdr rest)) (atomic-order-code (car rest) who))
        (else (error who "too many arguments"))))

    ;; ---- C primitive declarations (runtime/atomic.c) ----
    (define-c-primitive (%atomic-flag-make) any "npl_atomic_flag_make")
    (define-c-primitive (%atomic-flag? (value any)) any "npl_atomic_flag_p")
    (define-c-primitive (%atomic-flag-test-and-set! (value any) (order any))
      any "npl_atomic_flag_test_and_set")
    (define-c-primitive (%atomic-flag-clear! (value any) (order any))
      any "npl_atomic_flag_clear")

    (define-c-primitive (%atomic-box-make (value any)) any
      "npl_atomic_box_make")
    (define-c-primitive (%atomic-box? (value any)) any "npl_atomic_box_p")
    (define-c-primitive (%atomic-box-ref (value any) (order any)) any
      "npl_atomic_box_ref")
    (define-c-primitive (%atomic-box-set! (value any) (object any)
                          (order any)) any "npl_atomic_box_set")
    (define-c-primitive (%atomic-box-swap! (value any) (object any)
                          (order any)) any "npl_atomic_box_swap")
    (define-c-primitive (%atomic-box-cas (value any) (expected any)
                          (desired any) (order any)) any
      "npl_atomic_box_compare_and_swap")

    (define-c-primitive (%atomic-fxbox-make (value any)) any
      "npl_atomic_fxbox_make")
    (define-c-primitive (%atomic-fxbox? (value any)) any "npl_atomic_fxbox_p")
    (define-c-primitive (%atomic-fxbox-ref (value any) (order any)) any
      "npl_atomic_fxbox_ref")
    (define-c-primitive (%atomic-fxbox-set! (value any) (object any)
                          (order any)) any "npl_atomic_fxbox_set")
    (define-c-primitive (%atomic-fxbox-swap! (value any) (object any)
                          (order any)) any "npl_atomic_fxbox_swap")
    (define-c-primitive (%atomic-fxbox-cas (value any) (expected any)
                          (desired any) (order any)) any
      "npl_atomic_fxbox_compare_and_swap")
    (define-c-primitive (%atomic-fxbox-add (value any) (object any)
                          (order any)) any "npl_atomic_fxbox_add_fetch")
    (define-c-primitive (%atomic-fxbox-sub (value any) (object any)
                          (order any)) any "npl_atomic_fxbox_sub_fetch")
    (define-c-primitive (%atomic-fxbox-and (value any) (object any)
                          (order any)) any "npl_atomic_fxbox_and_fetch")
    (define-c-primitive (%atomic-fxbox-ior (value any) (object any)
                          (order any)) any "npl_atomic_fxbox_ior_fetch")
    (define-c-primitive (%atomic-fxbox-xor (value any) (object any)
                          (order any)) any "npl_atomic_fxbox_xor_fetch")

    (define-c-primitive (%atomic-pair-make (first any) (second any)) any
      "npl_atomic_pair_make")
    (define-c-primitive (%atomic-pair? (value any)) any "npl_atomic_pair_p")
    (define-c-primitive (%atomic-pair-ref (value any) (order any)) values
      "npl_atomic_pair_ref")
    (define-c-primitive (%atomic-pair-set! (value any) (first any)
                          (second any) (order any)) any
      "npl_atomic_pair_set")
    (define-c-primitive (%atomic-pair-swap! (value any) (first any)
                          (second any) (order any)) values
      "npl_atomic_pair_swap")
    (define-c-primitive (%atomic-pair-cas (value any) (expected-first any)
                          (expected-second any) (desired-first any)
                          (desired-second any) (order any)) values
      "npl_atomic_pair_compare_and_swap")

    (define-c-primitive (%atomic-fence (order any)) any "npl_atomic_fence")

    ;; ---- public optional-argument surface ----
    (define (make-atomic-flag)
      (%atomic-flag-make))

    (define (atomic-flag? object)
      (%atomic-flag? object))

    (define (atomic-flag-test-and-set! flag . rest)
      (%atomic-flag-test-and-set!
        flag (atomic-optional-order rest "atomic-flag-test-and-set!")))

    (define (atomic-flag-clear! flag . rest)
      (%atomic-flag-clear!
        flag (atomic-optional-order rest "atomic-flag-clear!")))

    (define (make-atomic-box object)
      (%atomic-box-make object))

    (define (atomic-box? object)
      (%atomic-box? object))

    (define (atomic-box-ref box . rest)
      (%atomic-box-ref box (atomic-optional-order rest "atomic-box-ref")))

    (define (atomic-box-set! box object . rest)
      (%atomic-box-set! box object
                        (atomic-optional-order rest "atomic-box-set!")))

    (define (atomic-box-swap! box object . rest)
      (%atomic-box-swap! box object
                         (atomic-optional-order rest "atomic-box-swap!")))

    (define (atomic-box-compare-and-swap! box expected desired . rest)
      (%atomic-box-cas box expected desired
                       (atomic-optional-order
                         rest "atomic-box-compare-and-swap!")))

    (define (make-atomic-fxbox object)
      (%atomic-fxbox-make object))

    (define (atomic-fxbox? object)
      (%atomic-fxbox? object))

    (define (atomic-fxbox-ref box . rest)
      (%atomic-fxbox-ref box (atomic-optional-order rest "atomic-fxbox-ref")))

    (define (atomic-fxbox-set! box object . rest)
      (%atomic-fxbox-set! box object
                          (atomic-optional-order rest "atomic-fxbox-set!")))

    (define (atomic-fxbox-swap! box object . rest)
      (%atomic-fxbox-swap! box object
                           (atomic-optional-order rest "atomic-fxbox-swap!")))

    (define (atomic-fxbox-compare-and-swap! box expected desired . rest)
      (%atomic-fxbox-cas box expected desired
                         (atomic-optional-order
                           rest "atomic-fxbox-compare-and-swap!")))

    (define (atomic-fxbox+/fetch! box object . rest)
      (%atomic-fxbox-add box object
                         (atomic-optional-order rest "atomic-fxbox+/fetch!")))

    (define (atomic-fxbox-/fetch! box object . rest)
      (%atomic-fxbox-sub box object
                         (atomic-optional-order rest "atomic-fxbox-/fetch!")))

    (define (atomic-fxbox-and/fetch! box object . rest)
      (%atomic-fxbox-and box object
                         (atomic-optional-order
                           rest "atomic-fxbox-and/fetch!")))

    (define (atomic-fxbox-ior/fetch! box object . rest)
      (%atomic-fxbox-ior box object
                         (atomic-optional-order
                           rest "atomic-fxbox-ior/fetch!")))

    (define (atomic-fxbox-xor/fetch! box object . rest)
      (%atomic-fxbox-xor box object
                         (atomic-optional-order
                           rest "atomic-fxbox-xor/fetch!")))

    (define (make-atomic-pair first second)
      (%atomic-pair-make first second))

    (define (atomic-pair? object)
      (%atomic-pair? object))

    (define (atomic-pair-ref pair . rest)
      (%atomic-pair-ref pair
                        (atomic-optional-order rest "atomic-pair-ref")))

    (define (atomic-pair-set! pair first second . rest)
      (%atomic-pair-set! pair first second
                         (atomic-optional-order rest "atomic-pair-set!")))

    (define (atomic-pair-swap! pair first second . rest)
      (%atomic-pair-swap! pair first second
                          (atomic-optional-order rest "atomic-pair-swap!")))

    (define (atomic-pair-compare-and-swap! pair expected-first
                                           expected-second desired-first
                                           desired-second . rest)
      (%atomic-pair-cas pair expected-first expected-second desired-first
                        desired-second
                        (atomic-optional-order
                          rest "atomic-pair-compare-and-swap!")))

    (define (atomic-fence order)
      (%atomic-fence (atomic-order-code order "atomic-fence")))))
