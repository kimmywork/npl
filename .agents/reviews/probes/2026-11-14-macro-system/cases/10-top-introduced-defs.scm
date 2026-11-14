(define-syntax def-getter (syntax-rules () ((_ name val) (begin (define tmp val) (define (name) tmp)))))
(def-getter get-a 1) (def-getter get-b 2)
(write (list (get-a) (get-b)))
