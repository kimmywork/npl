;; Copyright (C) 2020 Wolfgang Corcoran-Mathe

;; Permission is hereby granted, free of charge, to any person obtaining a
;; copy of this software and associated documentation files (the
;; "Software"), to deal in the Software without restriction, including
;; without limitation the rights to use, copy, modify, merge, publish,
;; distribute, sublicense, and/or sell copies of the Software, and to
;; permit persons to whom the Software is furnished to do so, subject to
;; the following conditions:

;; The above copyright notice and this permission notice shall be included
;; in all copies or substantial portions of the Software.

;; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
;; OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
;; MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
;; IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
;; CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
;; TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
;; SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

(define-record-type <just>
  (raw-just objs)
  just?
  (objs just-objs))

(define-record-type <nothing>
  (make-nothing)
  nothing?)

(define-record-type <left>
  (raw-left objs)
  left?
  (objs left-objs))

(define-record-type <right>
  (raw-right objs)
  right?
  (objs right-objs))

(define nothing-obj (make-nothing))

(define (nothing)
  nothing-obj)



; SRFI 189 helpers (the reference implementation assumes these).
(define (assume . args) #t)
(define (syntax-error message . irritants)
  (apply error message irritants))

;;;; Utility

(define-syntax const
  (syntax-rules ()
    ((_ obj) (lambda _ obj))))

(define (singleton? lis)
  (and (pair? lis) (null? (cdr lis))))

(define-syntax fast-apply
  (syntax-rules ()
    ((_ proc args)
     (if (singleton? args) (proc (car args)) (apply proc args)))))

(define-syntax fast-list->values
  (syntax-rules ()
    ((_ vals)
     (if (singleton? vals) (car vals) (apply values vals)))))

(define unspecified (if #f #f))


;;;; Constructors

(define (just . objs)
  (raw-just objs))

(define (left . objs)
  (raw-left objs))

(define (right . objs)
  (raw-right objs))

;;; List -> <container> equivalents of the basic constructors.

;;; Here and elsewhere, we copy caller-provided list arguments when
;;; passing them to raw-just/left/right, so that later mutation of the
;;; list argument doesn't affect the payloads of any Maybes/Eithers.

(define (list->just lis)
  (assume (or (null? lis) (pair? lis)))
  (raw-just (list-copy lis)))

(define (list->right lis)
  (assume (or (null? lis) (pair? lis)))
  (raw-right (list-copy lis)))

(define (list->left lis)
  (assume (or (null? lis) (pair? lis)))
  (raw-left (list-copy lis)))

(define (maybe->either maybe . default-objs)
  (assume (maybe? maybe))
  (if (nothing? maybe)
      (raw-left default-objs)
      (raw-right (just-objs maybe))))

(define (either->maybe either)
  (assume (either? either))
  (if (left? either)
      nothing-obj
      (raw-just (right-objs either))))

(define (either-swap either)
  (assume (either? either))
  (if (right? either)
      (raw-left (right-objs either))
      (raw-right (left-objs either))))


;;;; Predicates

(define (maybe? obj)
  (or (just? obj) (nothing? obj)))

;; True if all the maybes are Nothing, or if all are Justs containing
;; the same number of values which are element-wise equal in the sense
;; of `equal'.
(define (maybe= equal . maybes)
  (assume (procedure? equal))
  (assume (pair? maybes))
  (let ((maybe1 (car maybes)))
    (every (lambda (maybe2) (%maybe=2 equal maybe1 maybe2))
           (cdr maybes))))

;; Compare two Maybes.
(define (%maybe=2 equal maybe1 maybe2)
  (or (eqv? maybe1 maybe2)  ; Also handles the Nothing = Nothing case.
      (and (just? maybe1)
           (just? maybe2)
           (list= equal (just-objs maybe1) (just-objs maybe2)))))

(define (either? obj)
  (or (left? obj) (right? obj)))

;; True if the eithers are all Lefts/all Rights containing the same
;; number of values which are element-wise equal in the sense of `equal'.
(define (either= equal . eithers)
  (assume (procedure? equal))
  (assume (pair? eithers))
  (let ((either1 (car eithers)))
    (every (lambda (either2) (%either=2 equal either1 either2))
           (cdr eithers))))

;; Compare two Eithers.
(define (%either=2 equal either1 either2)
  (let ((e= (lambda (acc) (list= equal (acc either1) (acc either2)))))
    (or (eqv? either1 either2)
        (and (left? either1) (left? either2) (e= left-objs))
        (and (right? either1) (right? either2) (e= right-objs)))))


;;;; Accessors

;; Unwrap a Maybe.  If it's a Just, call the optional success
;; continuation (default: values) on its payload; otherwise,
;; call failure.
(define (maybe-ref maybe failure . %opt-args)
  (assume (maybe? maybe))
  (assume (procedure? failure))
  (if (just? maybe)
      (let ((objs (just-objs maybe))
            (success (if (pair? %opt-args) (car %opt-args) values)))
        (fast-apply success objs))
      (failure)))

;; Unwrap a Maybe.  If it's a Nothing, return the default objects.
(define (maybe-ref/default maybe . default-objs)
  (assume (maybe? maybe))
  (if (just? maybe)
      (let ((objs (just-objs maybe)))
        (fast-list->values objs))
      (fast-list->values default-objs)))

(define (%either-ref-single either accessor cont)
  (let ((objs (accessor either)))
    (fast-apply cont objs)))

;; Unwrap an Either, calling failure on the payload of a Left and the
;; optional success continuation (default: values) on that of a Right.
(define (either-ref either failure . %opt-args)
  (assume (either? either))
  (assume (procedure? failure))
  (if (right? either)
      (%either-ref-single either right-objs (if (pair? %opt-args)
                                                (car %opt-args)
                                                values))
      (%either-ref-single either left-objs failure)))

;; Unwrap an Either.  If it's a Left, return the default objects.
(define (either-ref/default either . default-objs)
  (assume (either? either))
  (if (right? either)
      (let ((objs (right-objs either)))
        (fast-list->values objs))
      (fast-list->values default-objs)))


;;;; Join and bind

;; If maybe is a Just containing a single Maybe, return that Maybe.
(define (maybe-join maybe)
  (assume (maybe? maybe))
  (if (nothing? maybe)
      nothing-obj
      (let ((objs (just-objs maybe)))
        (assume (and (singleton? objs) (maybe? (car objs)))
                "maybe-join: invalid payload")
        (car objs))))

;; Call the first mproc on the payload of a Maybe, producing a Maybe.
;; Repeat the operation with this Maybe and the remaining mprocs.
;; Return a Nothing immediately.
(define (maybe-bind maybe mproc . mprocs)
  (assume (maybe? maybe))
  (if (null? mprocs)
      (maybe-ref maybe nothing mproc)  ; fast path
      (let lp ((m maybe) (mp mproc) (mprocs mprocs))
        (maybe-ref m
                   nothing
                   (lambda objs
                     (if (null? mprocs)
                         (fast-apply mp objs) ; tail-call last
                         (lp (fast-apply mp objs)
                             (car mprocs)
                             (cdr mprocs))))))))

;; Compose the argument mprocs and return the resulting monadic procedure.
(define (maybe-compose . mprocs)
  (assume (pair? mprocs))
  (lambda args
    (let lp ((args args) (mproc (car mprocs)) (rest (cdr mprocs)))
      (if (null? rest)
          (fast-apply mproc args)             ; tail-call last
          (maybe-ref (fast-apply mproc args)
                     nothing
                     (lambda objs
                       (lp objs (car rest) (cdr rest))))))))

;; If either is a Right containing a single Either, return that Either.
(define (either-join either)
  (assume (either? either))
  (if (left? either)
      either
      (let ((objs (right-objs either)))
        (assume (and (singleton? objs) (either? (car objs)))
                "either-join: invalid payload")
        (car objs))))

;; Call the first mproc on the payload of a Either, producing a Either.
;; Repeat the operation with this Either and the remaining mprocs.
;; Return a Left immediately.
(define (either-bind either mproc . mprocs)
  (assume (either? either))
  (if (null? mprocs)
      (either-ref either (const either) mproc)  ; fast path
      (let lp ((e either) (mp mproc) (mprocs mprocs))
        (either-ref e
                    (const e)
                    (lambda objs
                      (if (null? mprocs)
                          (fast-apply mp objs)  ; tail-call last
                          (lp (fast-apply mp objs)
                              (car mprocs)
                              (cdr mprocs))))))))

;; Compose the argument mprocs and return the resulting monadic procedure.
(define (either-compose . mprocs)
  (assume (pair? mprocs))
  (lambda args
    (let lp ((args args) (mproc (car mprocs)) (rest (cdr mprocs)))
      (if (null? rest)
          (fast-apply mproc args)              ; tail-call last
          (either-ref (fast-apply mproc args)
                      left
                      (lambda objs
                        (lp objs (car rest) (cdr rest))))))))


;;;; Sequence operations

(define (maybe-length maybe)
  (assume (maybe? maybe))
  (if (just? maybe) 1 0))

;; Return maybe if its payload satisfies pred; otherwise, return Nothing.
(define (maybe-filter pred maybe)
  (assume (procedure? pred))
  (assume (maybe? maybe))
  (if (and (just? maybe) (fast-apply pred (just-objs maybe)))
      maybe
      nothing-obj))

;; Return maybe if its payload doesn't satisfy pred; otherwise, return
;; Nothing.
(define (maybe-remove pred maybe)
  (assume (procedure? pred))
  (assume (maybe? maybe))
  (if (and (just? maybe)
           (not (fast-apply pred (just-objs maybe))))
      maybe
      nothing-obj))

;; Traverse a container of Maybes with cmap, collect the payload
;; objects with aggregator, and wrap the new collection in a Just.
;; If a Nothing is encountered while traversing, return it
;; immediately.
(define maybe-sequence
  (case-lambda
   ((container cmap) (maybe-sequence container cmap list))
   ((container cmap aggregator)
    (assume (procedure? cmap))
    (assume (procedure? aggregator))
    (call-with-current-continuation
     (lambda (return)
       (just (cmap (lambda (m)
                     (maybe-ref m (lambda () (return m)) aggregator))
                   container)))))))

(define (either-length either)
  (assume (either? either))
  (if (right? either) 1 0))

;; Return either if its payload satisfies pred; otherwise, return
;; a Left of the default objects.
(define (either-filter pred either . default-objs)
  (assume (procedure? pred))
  (assume (either? either))
  (if (and (right? either) (fast-apply pred (right-objs either)))
      either
      (raw-left default-objs)))

(define (either-remove pred either . default-objs)
  (assume (procedure? pred))
  (assume (either? either))
  (if (and (right? either)
           (not (fast-apply pred (right-objs either))))
      either
      (raw-left default-objs)))

;; Traverse a container of Eithers with cmap, collect the payload
;; objects with aggregator, and wrap the new collection in a Right.
;; If a Left is encountered while traversing, return it immediately.
(define either-sequence
  (case-lambda
   ((container cmap) (either-sequence container cmap list))
   ((container cmap aggregator)
    (assume (procedure? cmap))
    (assume (procedure? aggregator))
    (call-with-current-continuation
     (lambda (return)
       (right (cmap (lambda (e)
                      (either-ref e (const (return e)) aggregator))
                    container)))))))


;;;; Protocol conversion

(define (maybe->list maybe)
  (assume (maybe? maybe))
  (if (nothing? maybe) '() (just-objs maybe)))

(define (either->list either)
  (assume (either? either))
  ((if (right? either) right-objs left-objs) either))

(define (list->maybe lis)
  (assume (or (null? lis) (pair? lis)))
  (if (null? lis) nothing-obj (raw-just (list-copy lis))))

(define (list->either lis . default-objs)
  (assume (or (null? lis) (pair? lis)))
  (if (null? lis)
      (raw-left default-objs)
      (raw-right (list-copy lis))))

;; If maybe is a Just, return its payload; otherwise, return false.
(define (maybe->truth maybe)
  (assume (maybe? maybe))
  (if (nothing? maybe)
      #f
      (begin
       (assume (singleton? (just-objs maybe))
               "maybe->truth: invalid payload")
       (car (just-objs maybe)))))

;; If either is a Right, return its payload; otherwise, return false.
(define (either->truth either)
  (assume (either? either))
  (if (left? either)
      #f
      (begin
       (assume (singleton? (right-objs either))
               "either->truth: invalid payload")
       (car (right-objs either)))))

(define (truth->maybe obj)
  (if obj (just obj) nothing-obj))

(define (truth->either obj . default-objs)
  (if obj (right obj) (raw-left default-objs)))

;;; These procedures interface between the Maybe protocol and the
;;; list-truth protocol, which uses #f to represent failure and a
;;; list to represent success.

(define (maybe->list-truth maybe)
  (assume (maybe? maybe))
  (if (just? maybe) (just-objs maybe) #f))

(define (either->list-truth either)
  (assume (either? either))
  (if (right? either) (right-objs either) #f))

;; If list-or-false is #f, return Nothing.  If it's a list, return a
;; Just containing its elements.
(define (list-truth->maybe list-or-false)
  (if list-or-false
      (begin
       (assume (or (null? list-or-false) (pair? list-or-false)))
       (raw-just (list-copy list-or-false)))
      nothing-obj))

;; If list-or-false is #f, return a Left of the default objects.
;; If it's a list, return a Right containing its elements.
(define (list-truth->either list-or-false . default-objs)
  (if list-or-false
      (begin
       (assume (or (null? list-or-false) (pair? list-or-false)))
       (raw-right (list-copy list-or-false)))
      (raw-left default-objs)))

;;; The following procedures interface between the Maybe protocol and
;;; the generation protocol, which uses an EOF object to represent
;;; failure and any other value to represent success.

;; If maybe is a Just whose payload is a single value, return that
;; value.  If it's a Nothing, return an eof-object.
(define (maybe->generation maybe)
  (assume (maybe? maybe))
  (if (nothing? maybe)
      (eof-object)
      (begin
       (assume (singleton? (just-objs maybe))
               "maybe->generation: invalid payload")
       (car (just-objs maybe)))))

;; If either is a Right whose payload is a single value, return that
;; value.  If it's a Left, return an eof-object.
(define (either->generation either)
  (assume (either? either))
  (if (left? either)
      (eof-object)
      (begin
       (assume (singleton? (right-objs either))
               "either->generation: invalid payload")
       (car (right-objs either)))))

(define (generation->maybe obj)
  (if (eof-object? obj) nothing-obj (just obj)))

(define (generation->either obj . default-objs)
  (if (eof-object? obj) (raw-left default-objs) (right obj)))

;;; These procedures interface between the Maybe/Either protocols and
;;; the values protocol, which returns one or more values to represent
;;; success and zero values to represent failure.

(define (maybe->values maybe)
  (maybe-ref maybe values values))

(define (values->maybe producer)
  (assume (procedure? producer))
  (call-with-values
   producer
   (lambda objs
     (if (null? objs) nothing-obj (raw-just objs)))))

;;; The following procedures interface between the Maybe protocol and
;;; the two-values protocol, which returns |#f, #f| to represent
;;; failure and |<any object>, #t| to represent success.

;; If maybe is Just containing a single value, return that value and #t.
;; If it's a Nothing, return |#f, #f|.
(define (maybe->two-values maybe)
  (assume (maybe? maybe))
  (if (nothing? maybe)
      (values #f #f)
      (begin
       (assume (singleton? (just-objs maybe))
               "maybe->two-values: invalid payload")
       (values (car (just-objs maybe)) #t))))

(define (two-values->maybe producer)
  (assume (procedure? producer))
  (call-with-values
   producer
   (lambda (obj success)
     (if success (just obj) nothing-obj))))

(define (either->values either)
  (either-ref/default either))

(define (values->either producer . default-objs)
  (assume (procedure? producer))
  (call-with-values
   producer
   (lambda objs
     (if (null? objs) (raw-left default-objs) (raw-right objs)))))

(define (exception->either pred thunk)
  (assume (procedure? pred))
  (assume (procedure? thunk))
  (guard (obj ((pred obj) (left obj)))
    (call-with-values thunk right)))
