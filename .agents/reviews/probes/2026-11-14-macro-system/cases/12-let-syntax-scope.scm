(write (list
  (let-syntax ((foo (syntax-rules () ((_) 'outer))))
    (let-syntax ((foo (syntax-rules () ((_) 'inner))) (bar (syntax-rules () ((_) (foo))))) (bar)))
  (let-syntax ((foo (syntax-rules () ((_) 'outer))))
    (letrec-syntax ((foo (syntax-rules () ((_) 'inner))) (bar (syntax-rules () ((_) (foo))))) (bar)))))
