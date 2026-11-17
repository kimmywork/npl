(import (scheme base) (scheme write))
(define-syntax fn
  (syntax-rules (=>)
    ;; 收集第一个 => 之前的参数
    ((_ "params" (arg ...) => form ...) (fn "body" (arg ...) form ...))
    ((_ "params" (arg ...) next more ...) (fn "params" (arg ... next) more ...))
    ;; body 里还有 =>：柯里化，剩下的部分再交给 \（宽松模式才允许这个模式）
    ((_ "body" formals form ... => more ...) (lambda formals (fn form ... => more ...)))
    ((_ "body" formals form ...) (lambda formals form ...))
    ;; 对外的写法
    ((_ => form ...) (fn "body" () form ...))
    ((_ () => form ...) (fn "body" () form ...))
    ((_ (arg0 arg ... . rest) => form ...) (fn "body" (arg0 arg ... . rest) form ...))
    ((_ arg0 more ...) (fn "params" (arg0) more ...))))

(define add3 (fn x => y z => w => (+ x y z w)))
(write (list ((fn x y => (+ x y)) 1 2)
             (((add3 1) 2 3) 4)
             (((fn (x . r) => y => (list x r y)) 1 2 3) 4)
             (((fn x => => x) 5))
             ((fn x => (display "") (* x x)) 6)
             ((fn x => (cond ((assoc x '((1 . one))) => cdr) (else 'none))) 1)))
(newline)
