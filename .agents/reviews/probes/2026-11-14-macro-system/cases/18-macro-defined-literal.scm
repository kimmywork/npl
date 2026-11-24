(define-syntax def-kw-macro
  (syntax-rules () ((_ name) (define-syntax name (syntax-rules (key) ((_ key) 'matched) ((_ other) 'not))))))
(def-kw-macro m)
(write (list (m key) (m 3)))
