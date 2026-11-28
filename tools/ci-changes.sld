; Classify changed repository paths into CI verification job flags.
; Pure Scheme so it runs identically under npli on Linux, macOS, and Windows.
(define-library (tools ci-changes)
  (export classify-changed-paths all-ci-suites)
  (import (scheme base)
          (only (npl string) string-prefix? string-suffix?))
  (begin
    (define CI-ALL-SUITES
      '("libraries" "runtime" "toolchain" "repl"
        "compiler" "fixtures" "unit_property"))

    (define (ci-any-prefix? path prefixes)
      (if (null? prefixes)
          #f
          (if (string-prefix? (car prefixes) path)
              #t
              (ci-any-prefix? path (cdr prefixes)))))

    (define (ci-any-exact? path names)
      (if (member path names) #t #f))

    (define (ci-flag-enabled? name enabled)
      (if (member name enabled) #t #f))

    (define (ci-add-flags names enabled)
      (let loop ((rest names) (out enabled))
        (if (null? rest)
            out
            (loop (cdr rest)
                  (if (member (car rest) out)
                      out
                      (cons (car rest) out))))))

    (define (ci-ast-lint-path? path)
      (if (ci-any-prefix? path '("runtime/" "src/" "lib/" "tools/ast-grep/"))
          #t
          (ci-any-exact? path
            '("sgconfig.yml" ".github/workflows/ci.yml"
              "tools/ci-changes.sld" "tools/ci-changes.npl"
              "tools/ci-changes-tests.npl"))))

    (define (ci-non-gate-path? path)
      (cond
        ((ci-any-prefix? path
           '("docs/" "bench/" "tools/ast-grep/"
             "bootstrap/stage0/" "bootstrap/compiler/"
             "bootstrap/runtime/" "tools/linux/"))
         #t)
        ((string-suffix? ".md" path) #t)
        ((ci-any-exact? path
           '("LICENSE" ".gitignore" "sgconfig.yml"
             "bootstrap/bootstrap.ssa"
             "tools/bench.py" "tools/gen_unicode.py"
             "tools/r7rs_inventory.py"))
         #t)
        (else #f)))

    (define (ci-core-infra-path? path)
      (cond
        ((ci-any-prefix? path
           '("src/compiler/" "external/" "cmake/"
             "tools/toolchain/" "tools/build_"))
         #t)
        ((ci-any-exact? path
           '("bootstrap/current.ssa" "CMakeLists.txt" "VERSION"
             ".github/workflows/ci.yml"
             "tools/ci-changes.sld" "tools/ci-changes.npl"
             "tools/ci-changes-tests.npl"
             "tools/run-tests.npl" "tools/run_tests.sh"
             "tools/toolchain.sld" "tools/utf8_locale.sh"
             "tools/CMakeLists.txt" "tools/c-struct-cache.npl"))
         #t)
        (else #f)))

    (define (ci-classify-one path enabled)
      (if (= (string-length path) 0)
          enabled
          (let ((enabled (if (ci-ast-lint-path? path)
                             (ci-add-flags '("ast_lint") enabled)
                             enabled)))
            (cond
              ((ci-non-gate-path? path) enabled)
              ((string-prefix? "src/compiler/tests/" path)
               (ci-add-flags '("compiler" "libraries" "repl" "toolchain")
                             enabled))
              ((ci-core-infra-path? path)
               (ci-add-flags CI-ALL-SUITES enabled))
              ((string-prefix? "runtime/" path)
               (ci-add-flags '("runtime" "compiler" "toolchain"
                               "repl" "fixtures" "unit_property")
                             enabled))
              ((string-prefix? "lib/" path)
               (ci-add-flags '("libraries" "compiler" "toolchain"
                               "repl" "fixtures" "unit_property")
                             enabled))
              ((string-prefix? "src/repl/" path)
               (ci-add-flags '("repl" "fixtures" "toolchain") enabled))
              ((string-prefix? "src/evaluator/" path)
               (ci-add-flags '("repl" "fixtures" "toolchain" "libraries")
                             enabled))
              ((string-prefix? "tests/fixtures/core-bindings/" path)
               (ci-add-flags '("fixtures" "repl" "libraries") enabled))
              ((string-prefix? "tests/fixtures/" path)
               (ci-add-flags '("fixtures" "libraries") enabled))
              ((string-prefix? "tests/unit/" path)
               (ci-add-flags '("unit_property" "compiler" "libraries")
                             enabled))
              ((string-prefix? "tests/property/" path)
               (ci-add-flags '("unit_property" "libraries") enabled))
              ((string-prefix? "tests/numeric/" path)
               (ci-add-flags '("runtime") enabled))
              ((if (string-prefix? "tools/libraries/" path)
                   #t
                   (ci-any-exact? path
                     '("tools/libraries.sld" "tools/libraries-tests.npl"
                       "tools/export-origins.npl")))
               (ci-add-flags '("libraries") enabled))
              ((string=? path "tools/check_full_compiler.sh")
               (ci-add-flags '("compiler") enabled))
              ((ci-any-exact? path
                 '("tools/check_relay_scripts.sh"
                   "tools/check_current_fixed_point.sh"
                   "tools/update_current_ssa.sh"
                   "tools/link-modes.sld"))
               (ci-add-flags '("toolchain") enabled))
              ((string=? path "tools/repl-suites.sld")
               (ci-add-flags '("repl") enabled))
              ((ci-any-exact? path
                 '("tools/expect-runner.npl" "tools/run_r7rs_fixture.sh"))
               (ci-add-flags '("fixtures" "repl") enabled))
              ; Unknown path: fail-safe to full verification.
              (else
               (ci-add-flags (cons "ast_lint" CI-ALL-SUITES) enabled))))))

    (define (ci-any-suite-enabled? enabled)
      (let loop ((rest CI-ALL-SUITES))
        (cond
          ((null? rest) #f)
          ((ci-flag-enabled? (car rest) enabled) #t)
          (else (loop (cdr rest))))))

    (define (ci-build-result enabled)
      (let ((upload (ci-any-suite-enabled? enabled)))
        (list (cons "ast_lint" (ci-flag-enabled? "ast_lint" enabled))
              (cons "upload" upload)
              (cons "libraries" (ci-flag-enabled? "libraries" enabled))
              (cons "runtime" (ci-flag-enabled? "runtime" enabled))
              (cons "toolchain" (ci-flag-enabled? "toolchain" enabled))
              (cons "repl" (ci-flag-enabled? "repl" enabled))
              (cons "compiler" (ci-flag-enabled? "compiler" enabled))
              (cons "fixtures" (ci-flag-enabled? "fixtures" enabled))
              (cons "unit_property"
                    (ci-flag-enabled? "unit_property" enabled)))))

    (define (all-ci-suites)
      (ci-build-result (cons "ast_lint" CI-ALL-SUITES)))

    (define (classify-changed-paths paths)
      (let loop ((rest paths) (enabled '()))
        (if (null? rest)
            (ci-build-result enabled)
            (loop (cdr rest) (ci-classify-one (car rest) enabled)))))))
