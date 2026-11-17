(define-syntax fn
  (syntax-rules (=>)
    ;; 内部：逐个收集 => 之前的参数
    ((_ "collect" (arg ...) => body ...) (lambda (arg ...) body ...))
    ((_ "collect" (arg ...) next rest ...) (fn "collect" (arg ... next) rest ...))
    ;; 公开形式
    ((_ => body ...) (lambda () body ...))
    ((_ () => body ...) (lambda () body ...))
    ((_ (arg0 arg ... . rest) => body ...) (lambda (arg0 arg ... . rest) body ...))
    ((_ arg0 rest ...) (fn "collect" (arg0) rest ...))))
(write (list ((fn => 0))
             ((fn x y => (+ x y)) 1 2)
             ((fn x => x) 7)
             ((fn (x . rest) => (list x rest)) 1 2 3 4)
             ((fn (x y) => (* x y)) 3 4)
             ((fn () => 'empty))
             ((fn x y z => (list z y x)) 1 2 3)))
(newline)
