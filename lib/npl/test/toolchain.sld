; Toolchain checks expressed in NPL.  Each function returns #t/#f and prints
; its own evidence; tools/run-tests.npl treats them like any other suite.
(define-library (npl test toolchain)
  (export check-gc check-current-fixed-point
          run-command capture-command string-contains? file-equal?
          line-count)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (scheme write)
          (npl process)
          (npl unix)
          (npl test expect))
  (begin
    (define (string-contains? text needle)
      (let ((limit (- (string-length text) (string-length needle))))
        (let loop ((index 0))
          (cond
            ((> index limit) #f)
            ((string=? (substring text index (+ index (string-length needle)))
                       needle)
             #t)
            (else (loop (+ index 1)))))))

    (define (file-equal? left right)
      (string=? (read-text-file left) (read-text-file right)))

    (define (line-count path)
      (let ((text (read-text-file path)))
        (let loop ((index 0) (count 0))
          (if (= index (string-length text))
              count
              (if (char=? (string-ref text index) #\newline)
                  (loop (+ index 1) (+ count 1))
                  (loop (+ index 1) count))))))

    (define (run-command . arguments)
      (run-process* arguments #f #f #f))

    (define (capture-command arguments input output error)
      (run-process* arguments input output error))

    (define (temp-file directory name)
      (let ((path (string-append directory "/" name)))
        path))

    (define (cleanup-file path)
      (if (file-exists? path) (delete-file path) #f))

    (define (check-gc root)
      (let ((build (string-append root "/build"))
            (work (make-temporary-directory))
            (nursery (get-environment-variable "NPL_GC_NURSERY_BYTES"))
            (verify (get-environment-variable "NPL_GC_VERIFY")))
        (let ((configured (= (run-command "cmake" "-S" root "-B" build
                                          "-G" "Ninja") 0))
              (built #f))
          (if configured
              (set! built (= (run-command "ninja" "-C" build "gc_stress") 0))
              #f)
          (if (not built)
              (begin
                (display "gc_stress build failed")
                (newline)
                #f)
              (let* ((first-out (temp-file work "first.out"))
                     (first-err (temp-file work "first.err"))
                     (first-status
                       (capture-command (list (string-append build
                                                             "/runtime/gc_stress"))
                                        #f first-out first-err))
                     (first-text (read-text-file first-out))
                     (first-ok
                       (if (= first-status 0)
                           (string-contains? first-text
                                             "PASS: 1000000 allocations")
                           #f)))
                (display first-text)
                (set-environment-variable! "NPL_GC_NURSERY_BYTES" "65536")
                (set-environment-variable! "NPL_GC_VERIFY" "1")
                (let* ((second-out (temp-file work "second.out"))
                       (second-err (temp-file work "second.err"))
                       (second-status
                         (capture-command (list (string-append build
                                                               "/runtime/gc_stress"))
                                          #f second-out second-err))
                       (second-text (read-text-file second-out))
                       (second-ok
                         (if (= second-status 0)
                             (if (string-contains? second-text
                                                   "PASS: 1000000 allocations")
                                 (if (string-contains? second-text
                                                       " minor collections")
                                     (if (string-contains?
                                           second-text ", 0 minor collections")
                                         #f
                                         #t)
                                     #f)
                                 #f)
                             #f)))
                  (display second-text)
                  (if nursery
                      (set-environment-variable! "NPL_GC_NURSERY_BYTES" nursery)
                      (delete-environment-variable! "NPL_GC_NURSERY_BYTES"))
                  (if verify
                      (set-environment-variable! "NPL_GC_VERIFY" verify)
                      (delete-environment-variable! "NPL_GC_VERIFY"))
                  (cleanup-file first-out)
                  (cleanup-file first-err)
                  (cleanup-file second-out)
                  (cleanup-file second-err)
                  (delete-directory work)
                  (if (if first-ok second-ok #f)
                      (begin
                        (display "runtime GC: PASS")
                        (newline)
                        #t)
                      #f)))))))

    (define (build-compiler qbe cc runtime input binary work stem)
      (let ((assembly (string-append work "/" stem ".s")))
        (if (not (= (run-command qbe "-o" assembly input) 0))
            #f
            (= (run-command cc "-o" binary assembly runtime "-lm") 0))))

    (define (emit-ssa compiler stdlib source output error)
      (let ((status
              (capture-command
                (list compiler "--emit-qbe" "--stdlib-root" stdlib source)
                #f output error)))
        (if (= status 0)
            (> (file-info:size (file-info output #t)) 0)
            #f)))

    (define (check-current-fixed-point root)
      (let ((build (string-append root "/build"))
            (runtime (string-append root "/build/runtime/libnpl_runtime.a"))
            (qbe (string-append root "/build/qbe/qbe"))
            (source (string-append root "/src/compiler/main.npl"))
            (stdlib (string-append root "/lib"))
            (seed (string-append root "/bootstrap/current.ssa"))
            (work (make-temporary-directory)))
        (let ((configured (= (run-command "cmake" "-S" root "-B" build
                                          "-G" "Ninja") 0))
              (built #f))
          (if configured
              (set! built (= (run-command "ninja" "-C" build
                                          "qbe_build" "npl_runtime") 0))
              #f)
          (if (not built)
              (begin
                (display "fixed-point build failed")
                (newline)
                #f)
              (let ((cc (if (get-environment-variable "NPL_CC")
                          (get-environment-variable "NPL_CC")
                          (if (get-environment-variable "CC")
                              (get-environment-variable "CC")
                              "cc")))
                  (current-bin (string-append work "/current"))
                    (gen1 (string-append work "/gen1.ssa"))
                    (gen1-bin (string-append work "/gen1"))
                    (gen2 (string-append work "/gen2.ssa"))
                    (gen2-bin (string-append work "/gen2"))
                    (gen3 (string-append work "/gen3.ssa"))
                    (error (string-append work "/error")))
                (let ((ok
                        (if (build-compiler qbe cc runtime seed current-bin
                                            work "current")
                            (if (emit-ssa current-bin stdlib source gen1 error)
                                (if (build-compiler qbe cc runtime gen1
                                                    gen1-bin work "gen1")
                                    (if (emit-ssa gen1-bin stdlib source gen2
                                                  error)
                                        (if (build-compiler qbe cc runtime
                                                            gen2 gen2-bin
                                                            work "gen2")
                                            (if (emit-ssa gen2-bin stdlib
                                                          source gen3 error)
                                                (if (file-equal? gen1 gen2)
                                                    (file-equal? gen2 gen3)
                                                    #f)
                                                #f)
                                            #f)
                                        #f)
                                    #f)
                                #f)
                            #f)))
                  (if ok
                      (begin
                        (display "current compiler fixed point: PASS")
                        (newline)
                        (display "SSA lines: ")
                        (display (line-count gen1))
                        (newline)
                        #t)
                      (begin
                        (display "current compiler fixed point: FAIL")
                        (newline)
                        (if (file-exists? error)
                            (begin (display (read-text-file error))
                                   (newline)))
                        #f))))))))))
