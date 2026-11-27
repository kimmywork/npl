;; (npl control parameters) — T4-D ordinary parameterizations and temporarily.
;;
;; This component adds first-class parameterization snapshots, the parameter?
;; predicate, and temporarily.  NPL's existing parameter objects already use
;; thread-owned copy-on-write bindings, so this prototype does not yet expose
;; SRFI 226's thread parameters or the fluid syntax; those need the shared-cell
;; versus inheritable-thread-local split described in the design.
(define-library (npl control parameters)
  (export make-parameter parameterize parameter?
          current-parameterization parameterization?
          call-with-parameterization temporarily)
  (import (scheme base))
  (begin
    (define-c-primitive (%parameter? (value any)) any "npl_parameter_p")
    (define-c-primitive (%parameterization-capture) any
      "npl_parameterization_capture")
    (define-c-primitive (%parameterization? (value any)) any
      "npl_parameterization_p")
    (define-c-primitive (%parameterization-enter (value any)) any
      "npl_parameterization_enter")
    (define-c-primitive (%parameterization-leave (value any)) any
      "npl_parameterization_leave")

    (define (parameter? object)
      (%parameter? object))

    (define (current-parameterization)
      (%parameterization-capture))

    (define (parameterization? object)
      (%parameterization? object))

    ;; A snapshot shares its binding chain; new bindings created through it
    ;; extend the snapshot's own head, while an enclosing extent keeps the
    ;; cells it already shares (the D0 shared-cell limitation).
    (define (call-with-parameterization parameterization thunk)
      (let ((saved (%parameterization-enter parameterization)))
        (dynamic-wind
          (lambda () (values))
          thunk
          (lambda () (%parameterization-leave saved)))))

    ;; temporarily works with any parameter-like procedure.  Each entry and
    ;; exit exchanges the pending values with the current values, as required
    ;; by SRFI 226; a converter runs again when a value is installed.
    (define-syntax temporarily
      (syntax-rules ()
        ((_ ((parameter-expression value-expression) ...) body ...)
         (let ((parameters (list parameter-expression ...))
               (pending-values (list value-expression ...)))
           (dynamic-wind
             (lambda ()
               (let ((current-values
                      (map (lambda (parameter) (parameter))
                           parameters)))
                 (for-each (lambda (parameter value) (parameter value))
                           parameters pending-values)
                 (set! pending-values current-values)))
             (lambda () body ...)
             (lambda ()
               (let ((current-values
                      (map (lambda (parameter) (parameter))
                           parameters)))
                 (for-each (lambda (parameter value) (parameter value))
                           parameters pending-values)
                 (set! pending-values current-values))))))))))
