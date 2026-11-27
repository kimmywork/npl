# An NPL Primer

> Adapted from [*A Scheme Primer*](https://files.spritely.institute/papers/scheme-primer.html)
> by Christine Lemmer-Webber and the Spritely Institute
> ([original source](https://gitlab.com/spritely/scheme-primer)).
>
> The original is dual-licensed under Apache-2.0 and
> [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). This NPL
> adaptation is distributed under CC BY 4.0. It was changed for NPL:
> setup, examples, keyword arguments, formatting, procedure printing, and
> reader extensions were updated. This adaptation is not endorsed by the
> original authors.

NPL is a self-hosting R7RS-small Scheme implementation with a native
compiler (`nplc`) and an evaluator/REPL (`npli`). This primer teaches the
Scheme language through NPL. Unless a section says otherwise, the examples
are standard R7RS-small and work in both `npli` and compiled `nplc`
programs.

## Contents

1. [Introduction](#1-introduction)
2. [Setting up](#2-setting-up)
3. [Hello Scheme](#3-hello-scheme)
4. [Basic types](#4-basic-types)
5. [Variables and procedures](#5-variables-and-procedures)
6. [Conditionals and predicates](#6-conditionals-and-predicates)
7. [Lists and cons](#7-lists-and-cons)
8. [Closures](#8-closures)
9. [Iteration and recursion](#9-iteration-and-recursion)
10. [Mutation and effects](#10-mutation-and-effects)
11. [Extensibility](#11-extensibility)
12. [Scheme in Scheme](#12-scheme-in-scheme)
13. [Guile to NPL](#13-guile-to-npl)

## 1. Introduction

Scheme is small, uniform, and extensible. Its core is a handful of rules
for forming expressions, with no restriction on how those expressions are
composed. The R5RS introduction put it well:

> Programming languages should be designed not by piling feature on top of
> feature, but by removing the weaknesses and restrictions that make
> additional features appear necessary.

NPL keeps that spirit: the compiler, the evaluator, the reader, and most of
the standard library are written in NPL itself. The language is small
enough to learn in an afternoon, but the same core supports macros,
first-class continuations, a numeric tower, records, pattern matching,
threads, and a C FFI.

This document starts with no assumption of programming experience. It ends
with a metacircular evaluator: a Scheme interpreter written in Scheme, in
about thirty lines.

## 2. Setting up

Build the toolchain from the repository root:

```sh
./tools/build_toolchain.sh
```

Then start the REPL:

```sh
./build/bin/npli
```

The prompt is `> `. To compile a program to a native executable:

```sh
./build/bin/nplc hello.npl -o hello
./hello
```

Every NPL program starts with imports. For this primer, start a REPL
session with:

```scheme
(import (scheme base)
        (scheme write)
        (scheme char)
        (npl format)
        (npl keyword))
```

`(scheme base)` has the core language, `(scheme write)` has `display` and
`write`, `(scheme char)` has `string-upcase`, `(npl format)` has `format`,
and `(npl keyword)` has the keyword predicates. `define*` and `lambda*`
are core syntax and do not need an import.

The examples use `> ` for input. The line after an input is what the REPL
prints. Definitions produce no printed value.

## 3. Hello Scheme

```scheme
(display "Hello world!\n")
```

This prints:

```text
Hello world!
```

`display` writes a string without quotes. `write` writes a value so that
the reader can read it back:

```scheme
> (display "hello")
hello
> (write "hello")
"hello"
> (newline)
```

Calling a procedure in Scheme puts the procedure first inside the
parentheses. Other languages write `display("hello")`; Scheme writes
`(display "hello")`.

## 4. Basic types

Arithmetic procedures are ordinary prefix procedures:

```scheme
> (+ 1 2)
3
> (/ 10 2)
5
> (/ 2 3)
2/3
> (* (- 8 (/ 30 5)) 21)
42
```

NPL has exact integers of arbitrary size, exact rationals, inexact
floating-point numbers, and rectangular complex numbers. Exact values are
not rounded unless you ask for it.

```scheme
42          ; integer
98.6        ; inexact floating point
2/3         ; exact rational
-42         ; negative
```

Booleans, strings, symbols, lists, and procedures are also values:

```scheme
#t                             ; true
#f                             ; false
"Pangalactic Gargleblaster"    ; string
'foo                           ; quoted symbol
'(1 2 3)                       ; quoted list
(lambda (x) (* x 2))           ; procedure
'(lambda (x) (* x 2))          ; list of symbols and numbers
```

The apostrophe is shorthand for `quote`. It stops evaluation:

```scheme
> 'foo
foo
> (quote foo)
foo
```

A symbol is not a string. Symbols are interned, compare with `eq?`, and are
the names used by programs and macros.

## 5. Variables and procedures

Use `define` to bind a value:

```scheme
> (define name "Jane")
> (string-append "Hello " name "!")
"Hello Jane!"
```

If the target is a list, `define` creates a procedure:

```scheme
(define (greet name)
  (string-append "Hello " name "!"))

> (greet "Samantha")
"Hello Samantha!"
```

The last expression in a procedure body is returned automatically. This is
the same definition written with `lambda`:

```scheme
(define greet
  (lambda (name)
    (string-append "Hello " name "!")))
```

`let` binds names for a body:

```scheme
> (let ((name "Horace"))
    (string-append "Hello " name "!"))
"Hello Horace!"
```

`let*` lets later bindings refer to earlier ones:

```scheme
> (let* ((name "Horace")
         (greeting (string-append "Hello " name "!\n")))
    (display greeting))
Hello Horace!
```

`apply` spreads a list as arguments:

```scheme
> (apply + '(1 2 5))
8
```

A dotted formal list collects the remaining arguments:

```scheme
(define (chatty-add chatty-name . nums)
  (format #t "<~a> If you add those together you get ~a!\n"
          chatty-name (apply + nums)))

> (chatty-add "Chester" 2 4 8 6)
<Chester> If you add those together you get 20!
```

### Optional and keyword arguments

NPL supports SRFI 89 `define*` and `lambda*`, plus the Guile-style
`#:optional`/`#:key` spelling. Both spellings use the same keyword objects:

```scheme
(define* (shopkeeper thing-to-buy
                     #:optional (how-many 1) (cost 20)
                     #:key (shopkeeper "Sammy")
                           (store "Plentiful Great Produce"))
  (format #t "You walk into ~a, grab something from the shelves,\n" store)
  (display "and walk up to the counter.\n\n")
  (format #t "~a looks at you and says, " shopkeeper)
  (format #t "'~a ~a, eh? That'll be ~a coins!'\n"
          how-many thing-to-buy (* cost how-many)))

> (shopkeeper "apples")
You walk into Plentiful Great Produce, grab something from the shelves,
and walk up to the counter.

Sammy looks at you and says, '1 apples, eh? That'll be 20 coins!'
```

Keyword arguments can be passed in any order:

```scheme
> (shopkeeper "screws" 3 2
              #:shopkeeper "Horace"
              #:store "Horace's Hardware")
You walk into Horace's Hardware, grab something from the shelves,
and walk up to the counter.

Horace looks at you and says, '3 screws, eh? That'll be 6 coins!'
```

The SRFI 89 spelling uses `keyword:` objects and parenthesized optional
parameters. Trailing-colon keyword reading is opt-in, so start the file
with `#!colon-keyword`:

```scheme
#!colon-keyword

(define* (g a (b 1) (c: c 2))
  (list a b c))

> (g 10 20 c: 30)
(10 20 30)
```

Keywords whose names contain spaces or delimiters use the barred spelling,
for example `(string->keyword "a b")` writes as `#:|a b|`. The barred
spelling preserves case under `#!fold-case`.

`#:optional`/`#:key` are NPL's Guile-compatible extensions; the canonical
SRFI 89 spelling is `(b 1)` and `(c: c 2)`. The `c:` spelling requires
`#!colon-keyword` at the top of the source (`#!srfi-88` remains an alias);
the `#:c` spelling always works.

Unknown keyword arguments raise an error by default. Put
`#:allow-other-keys` in the formal list to ignore them instead:

```scheme
(define* (f a #:key x #:allow-other-keys)
  (list a x))

> (f 1 #:x 2 #:ignored 3)
(1 2)
```

A procedure can return multiple values:

```scheme
(define (add-and-multiply x y)
  (values (+ x y) (* x y)))

> (add-and-multiply 2 8)
10
16
> (define-values (added multiplied) (add-and-multiply 3 10))
> added
13
> multiplied
30
```

`let-values` and `call-with-values` are also available.

## 6. Conditionals and predicates

Predicates end in `?`:

```scheme
> (string? "apple")
#t
> (string? 128)
#f
> (string? 'apple)
#f
```

`if` evaluates a test and returns the chosen branch:

```scheme
(if <test>
    <consequent>
    <alternate>)
```

The alternate is optional. `cond` is clearer for multiple cases:

```scheme
(define (goldilocks n smallest-ok biggest-ok)
  (cond
   ((< n smallest-ok) "Too small!")
   ((> n biggest-ok) "Too big!")
   (else "Just right!")))

> (goldilocks 3 10 20)
"Too small!"
> (goldilocks 33 10 20)
"Too big!"
> (goldilocks 12 10 20)
"Just right!"
```

`when` and `unless` are convenient for one-armed conditionals:

```scheme
(when (> n 0)
  (display "positive\n"))
```

`equal?` compares structure; `eq?` compares identity:

```scheme
> (define a-list (list 1 2 3))
> (define b-list (list 1 2 3))
> (equal? a-list b-list)
#t
> (eq? a-list b-list)
#f
```

### Curly infix (SRFI 105)

NPL's readers also accept SRFI 105 curly-infix expressions. A simple curly
list maps to a prefix call:

```scheme
> {8 > 9}
#f
> {1 + 2 + 3}
6
> (let ((a 2) (b 3) (c 4))
    {a * {b + c}})
14
```

`{}` is the empty list, `{5}` is `5`, and `{- 5}` is `(- 5)`. A mixed
expression such as `{4 + 5 * 6}` maps to `($nfx$ 4 + 5 * 6)`; import
`(npl infix)` to get a clear error handler, or define `$nfx$` yourself.
Core curly-infix support is enabled by default.

Start a source file with `#!curly-infix` to also enable the neoteric forms
from SRFI 105. Adjacency is significant: `f(x)` is `(f x)`, while `f (x)`
is two separate data.

```scheme
#!curly-infix
(import (scheme base) (npl infix))

(define (increment x) (+ x 1))
```

```scheme
> increment(41)
42
> (let ((double (lambda (x) (* x 2))))
    double{20 + 1})
42
> (vector 10 20 30)[1]
20
```

`f(x)`, `f[x]`, and `f{x}` are the neoteric application, bracket-apply,
and curly-argument forms. `'f(1)` reads as the datum `(f 1)`, and
`f(. rest)` is the improper call `(f . rest)`. Dotted curly lists stay
improper and are mixed: `{a + b . c}` reads as `($nfx$ a + b . c)`. The
`#!curly-infix` activation is one-way and lasts for the rest of the file;
there is no `#!no-curly-infix`. A fresh file or port starts from the
default reader behavior.

Reader directives are one-way activations. `#!colon-keyword` enables
trailing-colon keywords (`foo:`), and `#!srfi-88` remains a compatibility
alias. The `#:foo` spelling always works without a directive. R7RS's
`#!fold-case` / `#!no-fold-case` pair remains the one standardized
toggle.

Parameterized directives use one parenthesized list:

```scheme
#!(curly-infix neoteric)
#!(colon-keyword)
```

`#!(curly-infix)` is equivalent to `#!curly-infix`, and
`#!(curly-infix neoteric)` enables the neoteric forms as well. Directives
adjust the reader; they do not select a language.

Anything that is not `#f` is true. `member` returns the remaining list when
it finds a match, and `#f` otherwise:

```scheme
> (member 'b '(a b c))
(b c)
> (member 'z '(a b c))
#f
```

## 7. Lists and cons

Lists are the fundamental compound data structure:

```scheme
> (list 1 2 "cat" 33.8 'foo)
(1 2 "cat" 33.8 foo)
> '(1 2 "cat" 33.8 foo)
(1 2 "cat" 33.8 foo)
```

Quoting is cumulative; a quoted list does not evaluate its members. The
empty list is `'()`; its predicate is `null?`.

A list is a chain of pairs ending in the empty list:

```scheme
> (cons 'a '())
(a)
> (cons 'a (cons 'b (cons 'c '())))
(a b c)
> (cons 'a 'b)
(a . b)
```

`car` returns the first element of a pair; `cdr` returns the rest:

```scheme
> (car '(a b c))
a
> (cdr '(a b c))
(b c)
> (car (cdr '(a b c)))
b
```

An association list (alist) maps keys to values:

```scheme
(define animal-noises
  '((cat . meow)
    (dog . woof)
    (sheep . baa)))

> (assoc 'cat animal-noises)
(cat . meow)
> (assoc 'alien animal-noises)
#f
```

NPL also has hash tables in `(npl hashtable)` when an alist is too slow.

Quasiquote moves between data and code. A backtick starts a template, and a
comma evaluates one hole:

```scheme
(define (cat-years years)
  (cond
   ((<= years 1) (* years 15))
   ((<= years 2) (+ 15 (* 9 (- years 1))))
   (else (+ 24 (* 4 (- years 2))))))

(define (cat-entry name age)
  `(cat (name ,name)
        (age ,age)
        (cat-years-age ,(cat-years age))))

> (cat-entry "Missy Rose" 16)
(cat (name "Missy Rose") (age 16) (cat-years-age 80))
```

Dotted-tail unquote is supported too:

```scheme
> (let ((b 42)) `(a . ,b))
(a . 42)
```

## 8. Closures

A closure captures the lexical environment in which it was created. For
example, we can make a version of Goldilocks that remembers its range:

```scheme
(define (make-goldilocks smallest-ok biggest-ok)
  (define (goldilocks n)
    (cond
     ((< n smallest-ok) "Too small!")
     ((> n biggest-ok) "Too big!")
     (else "Just right!")))
  goldilocks)

> (define goldi (make-goldilocks 10 30))
> (goldi 7)
"Too small!"
> (goldi 256)
"Too big!"
> (goldi 22)
"Just right!"
```

The inner `goldilocks` procedure closes over `smallest-ok` and
`biggest-ok`; the returned closure remembers them.

Closures can also implement data structures. This is a pair built from
procedures:

```scheme
(define (abstract-cons car-data cdr-data)
  (lambda (method)
    (cond
     ((eq? method 'car) car-data)
     ((eq? method 'cdr) cdr-data)
     (else (error "Unknown method:" method)))))

> (define our-cons (abstract-cons 'foo 'bar))
> (our-cons 'car)
foo
> (our-cons 'cdr)
bar
```

NPL prints anonymous procedure values as `#<procedure>`. Top-level and
library procedures include names when known, for example
`#<procedure scheme/base:map>`. A closure bound directly by `let`, `let*`,
`letrec`, an internal definition, or `set!` takes the binding's source name,
such as `#<procedure local>`. Aliasing an existing procedure keeps its
original name, so a local alias of `car` still prints as
`#<procedure car>`.

## 9. Iteration and recursion

`map` applies a procedure to every element and returns a new list:

```scheme
> (map string-length '("cat" "dog" "gorilla" "salamander"))
(3 3 7 10)
```

`for-each` is like `map` but is used for effect:

```scheme
> (for-each (lambda (str)
              (display (string-append "I just love "
                                      (string-upcase str)
                                      "!!!\n")))
            '("ice cream" "fudge" "cookies"))
I just love ICE CREAM!!!
I just love FUDGE!!!
I just love COOKIES!!!
```

Scheme iteration is built on recursion. Here is a simple `for-each`:

```scheme
(define (my-for-each proc lst)
  (if (eq? lst '())
      'done
      (let ((item (car lst)))
        (proc item)
        (my-for-each proc (cdr lst)))))
```

The recursive call is in tail position. NPL, like Scheme, performs proper
tail calls, so this loop does not grow the stack.

A procedure that is not tail-recursive builds work on the stack:

```scheme
(define (build-tree depth)
  (if (= depth 0)
      '(0)
      (list depth
            (build-tree (- depth 1))
            (build-tree (- depth 1)))))

> (build-tree 3)
(3 (2 (1 (0) (0)) (1 (0) (0))) (2 (1 (0) (0)) (1 (0) (0))))
```

`letrec` allows mutually recursive bindings:

```scheme
(letrec ((alice
          (lambda (first?)
            (report-status "Alice" first?)
            (if first? (bob #f))))
         (bob
          (lambda (first?)
            (report-status "Bob" first?)
            (if first? (alice #f))))
         (report-status
          (lambda (name first?)
            (display
             (string-append name " is "
                            (if first? "first" "second")
                            "!\n")))))
  (alice #t)
  (display "-----\n")
  (bob #t))
```

A named `let` is a convenient loop:

```scheme
> (let loop ((words '("carrot" "potato" "pea" "celery"))
             (num-words 0)
             (num-chars 0))
    (if (eq? words '())
        (format #f "We found ~a words and ~a chars!"
                num-words num-chars)
        (loop (cdr words)
              (+ num-words 1)
              (+ num-chars (string-length (car words))))))
"We found 4 words and 21 chars!"
```

## 10. Mutation and effects

`set!` changes an existing binding:

```scheme
> (define chest 'sword)
> (set! chest 'gold)
> chest
gold
```

Closures plus `set!` can introduce state:

```scheme
(define (make-countdown n)
  (lambda ()
    (define last-n n)
    (if (zero? n)
        0
        (begin
          (set! n (- n 1))
          last-n))))

> (define cdown (make-countdown 3))
> (cdown)
3
> (cdown)
2
> (cdown)
1
> (cdown)
0
> (cdown)
0
```

`begin` sequences expressions and returns the last value. It is needed when
an effect must happen before a value is returned.

Vectors are fixed-length mutable sequences:

```scheme
> (define vec (vector 'a 'b 'c))
> (vector-ref vec 1)
b
> (vector-set! vec 1 'boop)
> vec
#(a boop c)
```

A vector literal, such as `#(a b c)`, is immutable in NPL. Use `vector` to
make a mutable vector.

`display` writes to the current output port. Ports can also be passed
explicitly:

```scheme
> (let ((p (open-output-string)))
    (display "hello" p)
    (get-output-string p))
"hello"
```

Mutation introduces time and change: a procedure may return different
results for the same arguments. That is powerful, and it should be used
deliberately.

## 11. Extensibility

Scheme programs can define new syntax. Suppose we want `when`:

```scheme
(when (our-test)
  (do-thing-1)
  (do-thing-2))
```

`when` cannot be an ordinary procedure because its body must not run unless
the test is true. It is syntax. NPL uses hygienic `syntax-rules`:

```scheme
(define-syntax my-when
  (syntax-rules ()
    ((_ test body ...)
     (if test (begin body ...)))))
```

`my-when` is renamed only to avoid shadowing the built-in `when`.

The original primer uses Guile's `define-syntax-rule` shorthand. NPL does
not ship that name, but it can be defined with `syntax-rules`:

```scheme
(define-syntax define-syntax-rule
  (syntax-rules ()
    ((define-syntax-rule (id pattern ...) template)
     (define-syntax id
       (syntax-rules ()
         ((id pattern ...) template))))))
```

Now we can write:

```scheme
(define-syntax-rule (for (item lst) body ...)
  (for-each (lambda (item) body ...) lst))

> (for (str '("strawberries" "bananas" "grapes"))
    (display (string-append "I just love "
                            (string-upcase str)
                            "!!!\n")))
I just love STRAWBERRIES!!!
I just love BANANAS!!!
I just love GRAPES!!!
```

Macros can build larger abstractions. This simplified `methods` macro
implements objects with symbol-dispatched methods:

```scheme
(define-syntax-rule (methods ((method-id method-args ...)
                              body ...) ...)
  (lambda (method . args)
    (letrec ((method-id
              (lambda (method-args ...)
                body ...)) ...)
      (cond
       ((eq? method 'method-id)
        (apply method-id args)) ...
       (else
        (error "No such method:" method))))))

(define (make-enemy name hp)
  (methods
   ((get-name)
    name)
   ((damage-me weapon hp-lost)
    (cond
     ((dead?)
      (format #t "Poor ~a is already dead!\n" name))
     (else
      (set! hp (- hp hp-lost))
      (format #t "You attack ~a, doing ~a damage!\n"
              name hp-lost))))
   ((dead?)
    (<= hp 0))))

> (define hobgob (make-enemy "Hobgoblin" 25))
> (hobgob 'get-name)
"Hobgoblin"
> (hobgob 'dead?)
#f
> (hobgob 'damage-me "club" 10)
You attack Hobgoblin, doing 10 damage!
```

NPL also has procedural `syntax-case` for macros that need to inspect or
compute syntax, and a built-in `match` form for pattern matching.

`define-macro` is a non-hygienic macro facility found in some
implementations, including Guile. NPL does not provide it; `syntax-rules`
and `syntax-case` are the supported ways to define syntax.

## 12. Scheme in Scheme

Here is a small evaluator written in NPL. `match` is built into NPL, so no
import is needed.

```scheme
(define (env-lookup env name)
  (match (assoc name env)
    ((_key . val) val)
    (_ (error "Variable unbound:" name))))

(define (extend-env env names vals)
  (if (eq? names '())
      env
      (cons (cons (car names) (car vals))
            (extend-env env (cdr names) (cdr vals)))))

(define (evaluate expr env)
  (match expr
    ;; builtin types
    ((or #t #f (? number?)) expr)
    ;; quoting
    (('quote quoted-expr) quoted-expr)
    ;; variable lookup
    ((? symbol? name) (env-lookup env name))
    ;; conditionals
    (('if test consequent alternate)
     (if (evaluate test env)
         (evaluate consequent env)
         (evaluate alternate env)))
    ;; lambdas
    (('lambda (args ...) body)
     (lambda vals
       (evaluate body (extend-env env args vals))))
    ;; application
    ((proc-expr arg-exprs ...)
     (apply (evaluate proc-expr env)
            (map (lambda (arg-expr)
                   (evaluate arg-expr env))
                 arg-exprs)))))
```

The evaluator takes an expression and an association-list environment. The
`match` clauses try in order; `?` applies a predicate, and quoted symbols
match literal syntax.

An environment for arithmetic:

```scheme
(define math-env
  `((+ . ,+)
    (- . ,-)
    (* . ,*)
    (/ . ,/)))

> (evaluate '(* (- 8 (/ 30 5)) 21) math-env)
42
> (evaluate '((lambda (x) (* x x)) 4) math-env)
16
```

A Fibonacci program that uses only `+` and `=`:

```scheme
(define fib-program
  '((lambda (prog arg)
      (prog prog arg))
    (lambda (fib n)
      (if (= n 0)
          0
          (if (= n 1)
              1
              (+ (fib fib (+ n -1))
                 (fib fib (+ n -2))))))
    10))

(define fib-env
  `((+ . ,+)
    (= . ,=)))

> (evaluate fib-program fib-env)
55
```

The evaluator is deliberately small, but it can compute anything that is
computable. It is also capability-limited: the interpreted program can only
call procedures that you put in its environment. NPL itself is a full
language with POSIX and FFI access, so this is a property of the evaluator's
environment, not a claim that every NPL program is sandboxed.

## 13. Guile to NPL

| Guile | NPL |
|---|---|
| `(use-modules (ice-9 match))` | `match` is built in; `(import (npl match))` is optional |
| `(format #t "~a" x)` | `(import (npl format))` and the same `format` call |
| `(define* (f a #:optional b #:key c) ...)` | `define*`, `#:optional`, `#:key` (NPL extension) |
| `(f 1 c: 2)` / `(f 1 #:c 2)` | both spellings are accepted |
| `(define-macro ...)` | `define-syntax` + `syntax-rules`, or `syntax-case` |
| `REPL>` | `> ` |
| `(lambda (. vals) ...)` | `(lambda vals ...)` |
| `string-upcase` | import `(scheme char)` |
| `#<procedure foo (x)>` | `#<procedure foo>` or `#<procedure scheme/base:map>` |

See the [NPL README](https://github.com/kimmywork/npl#readme) for the full
library list and build instructions.

## License

This adaptation is distributed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The original *A Scheme Primer* is by Christine Lemmer-Webber and the
Spritely Institute, and is dual-licensed under Apache-2.0 and CC BY 4.0.
The original source is at
[https://gitlab.com/spritely/scheme-primer](https://gitlab.com/spritely/scheme-primer).
