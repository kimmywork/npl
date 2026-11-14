(define-syntax def-getter (syntax-rules () ((_ name val) (begin (define tmp val) (define (name) tmp)))))
(write (let () (def-getter get-a 1) (def-getter get-b 2) (list (get-a) (get-b))))
