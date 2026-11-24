(define-syntax \
  (syntax-rules (=>)
    ((_ => body ...) (lambda () body ...))
    ((_ arg ... => body ...) (lambda (arg ...) body ...))))
(define f (\ x => y => (+ x y)))
(write (f 1 'ignored 2)) (newline)
