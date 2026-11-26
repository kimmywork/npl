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

    ;; The snapshot owns a clone of the binding chain.  Install another clone
    ;; so mutations inside the thunk never leak back into the snapshot.
    (define (call-with-parameterization parameterization thunk)
      (let ((saved (%parameterization-enter parameterization)))
        (dynamic-wind
          (lambda () (values))
          thunk
          (lambda () (%parameterization-leave saved)))))

    ;; temporarily works with any parameter-like procedure: it saves the old
    ;; values, installs the new ones on entry, and restores the old ones on
    ;; every exit (including continuation exits through dynamic-wind).
    (define-syntax temporarily
      (syntax-rules ()
        ((_ ((parameter-expression value-expression) ...) body ...)
         (let ((parameters (list parameter-expression ...))
               (new-values (list value-expression ...)))
           (let ((old-values (map (lambda (parameter) (parameter))
                                  parameters)))
             (dynamic-wind
               (lambda ()
                 (for-each (lambda (parameter value) (parameter value))
                           parameters new-values))
               (lambda () body ...)
               (lambda ()
                 (for-each (lambda (parameter value) (parameter value))
                           parameters old-values))))))))))
