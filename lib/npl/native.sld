; Loading native packages at run time (deployment mode B).
;
; A package build produces a shared object named libnpl_<name>; this library
; finds it, loads it, and binds one exported NplVal function.  Core packages
; use linked mode (A) instead and do not import this library.
;
; Search order:
;   1. NPL_NATIVE_PATH, a colon-separated list of directories;
;   2. <executable-dir>/../lib/npl/native (the install/build layout);
;   3. <executable-dir>/../lib/npl;
;   4. ./lib/npl/native (a convenient checkout default).
; Each directory is tried with lib<name>.dylib, lib<name>.so, <name>.dylib,
; and <name>.so.
(define-library (npl native)
  (export native-library-path load-native-library native-procedure
          native-candidates)
  (import (scheme base)
          (scheme file)
          (scheme process-context)
          (only (npl string) string-suffix?)
          (only (npl util) path-directory)
          (npl process)
          (npl ffi))
  (begin
    (define (native-file? path)
      (if (if (string-suffix? ".dylib" path) #t
              (if (string-suffix? ".so" path) #t #f))
          (file-exists? path)
          #f))

    (define (explicit-native-path? name)
      (if (string-suffix? ".dylib" name) #t
          (if (string-suffix? ".so" name) #t
              (let loop ((index 0))
                (if (= index (string-length name))
                    #f
                    (if (char=? (string-ref name index) #\/)
                        #t
                        (loop (+ index 1))))))))

    (define (native-file-names name)
      (list (string-append "lib" name ".dylib")
            (string-append "lib" name ".so")
            (string-append name ".dylib")
            (string-append name ".so")))

    (define (native-files-in directory name)
      (map (lambda (file)
             (if (if (string=? directory "")
                     #t
                     (char=? (string-ref directory
                                         (- (string-length directory) 1))
                             #\/))
                 (string-append directory file)
                 (string-append directory "/" file)))
           (native-file-names name)))

    (define (split-colon text)
      (let loop ((start 0) (index 0) (parts '()))
        (cond
          ((= index (string-length text))
           (reverse (cons (substring text start index) parts)))
          ((char=? (string-ref text index) #\:)
           (loop (+ index 1) (+ index 1)
                 (cons (substring text start index) parts)))
          (else (loop start (+ index 1) parts)))))

    (define (non-empty strings)
      (cond
        ((null? strings) '())
        ((string=? (car strings) "") (non-empty (cdr strings)))
        (else (cons (car strings) (non-empty (cdr strings))))))

    (define (native-directories)
      (let ((configured (get-environment-variable "NPL_NATIVE_PATH"))
            (executable (executable-path)))
        (append
          (if configured (non-empty (split-colon configured)) '())
          (if executable
              (let ((directory (path-directory executable)))
                (list (string-append directory "../lib/npl/native/")
                      (string-append directory "../lib/npl/")))
              '())
          (list "lib/npl/native/"))))

    (define (native-candidates name)
      (if (explicit-native-path? name)
          (list name)
          (apply append
                 (map (lambda (directory)
                        (native-files-in directory name))
                      (native-directories)))))

    (define (native-library-path name)
      (let loop ((candidates (native-candidates name)))
        (cond
          ((null? candidates) #f)
          ((native-file? (car candidates)) (car candidates))
          (else (loop (cdr candidates))))))

    (define (load-native-library name)
      (let ((path (native-library-path name)))
        (if (not path)
            (error "native package not found" name (native-candidates name)))
        (let ((loaded (ffi-load-library path)))
          (if (string? loaded)
              (error "native package failed to load" name path loaded)
              path))))

    (define (native-procedure symbol arity)
      (let ((procedure (ffi-procedure symbol arity)))
        (if procedure
            procedure
            (error "native function not found" symbol arity)))))
)
