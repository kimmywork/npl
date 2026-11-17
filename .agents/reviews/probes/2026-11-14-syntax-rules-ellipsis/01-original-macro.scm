(define-syntax \
  (syntax-rules (=>)
    ((_ => body ...) (lambda () body ...))
    ((_ arg ... => body ...) (lambda (arg ...) body ...))
    ((_ (arg ... . rest) => body ...) (lambda (arg ... . rest) body ...))
    ((_ (arg ...) => body ...) (lambda (arg ...) body ...))))
(define-syntax probe
  (syntax-rules (=>)
    ((_ arg ... => body ...) '(rule2 (arg ...) (body ...)))
    ((_ (arg ... . rest) => body ...) '(rule3 (arg ...) rest))))
(write ((\ x y => (+ x y)) 1 2)) (newline)
(write (probe (x . rest) => (list x rest))) (newline)
(write ((\ (x . rest) => (list x rest)) 1 2 3 4)) (newline)
