#!/bin/bash
# Classify changed files into CI job flags (stdout: key=true|false lines).
# Usage:
#   tools/ci_changes.sh --all
#   tools/ci_changes.sh --base <git-ref>
#   tools/ci_changes.sh < file-list.txt
set -eu

ast_lint=false
libraries=false
runtime=false
toolchain=false
repl=false
compiler=false
fixtures=false
unit_property=false

enable_all_suites() {
    libraries=true
    runtime=true
    toolchain=true
    repl=true
    compiler=true
    fixtures=true
    unit_property=true
}

emit_outputs() {
    local build=false
    if [ "$libraries" = true ] || [ "$runtime" = true ] || \
       [ "$toolchain" = true ] || [ "$repl" = true ] || \
       [ "$compiler" = true ] || [ "$fixtures" = true ] || \
       [ "$unit_property" = true ]; then
        build=true
    fi
    printf 'ast_lint=%s\n' "$ast_lint"
    printf 'build=%s\n' "$build"
    printf 'libraries=%s\n' "$libraries"
    printf 'runtime=%s\n' "$runtime"
    printf 'toolchain=%s\n' "$toolchain"
    printf 'repl=%s\n' "$repl"
    printf 'compiler=%s\n' "$compiler"
    printf 'fixtures=%s\n' "$fixtures"
    printf 'unit_property=%s\n' "$unit_property"
}

classify_path() {
    local path=$1
    [ -n "$path" ] || return 0

    case "$path" in
        runtime/*|src/*|lib/*|tools/ast-grep/*|sgconfig.yml|.github/workflows/ci.yml|tools/ci_changes.sh)
            ast_lint=true
            ;;
    esac

    case "$path" in
        # Docs, lint-only, benchmarks, and frozen bootstrap do not affect the main gate.
        docs/*|*.md|LICENSE|bench/*|.gitignore|tools/ast-grep/*|sgconfig.yml|\
        bootstrap/stage0/*|bootstrap/compiler/*|bootstrap/runtime/*|bootstrap/bootstrap.ssa|\
        tools/linux/*|tools/bench.py|tools/gen_unicode.py|tools/r7rs_inventory.py)
            ;;
        # Compiler tests are also read by driver, REPL, and coverage checks.
        src/compiler/tests/*)
            compiler=true
            libraries=true
            repl=true
            toolchain=true
            ;;
        # Compiler core, seed, QBE, build system, and runner orchestration -> full suite.
        src/compiler/*|bootstrap/current.ssa|external/*|CMakeLists.txt|cmake/*|VERSION|\
        .github/workflows/ci.yml|tools/ci_changes.sh|tools/run-tests.npl|tools/run_tests.sh|\
        tools/build_*.sh|tools/toolchain.sld|tools/toolchain/*|tools/utf8_locale.sh|\
        tools/CMakeLists.txt|tools/c-struct-cache.npl)
            enable_all_suites
            ;;
        # C runtime affects all execution suites but not Scheme library metadata.
        runtime/*)
            runtime=true
            compiler=true
            toolchain=true
            repl=true
            fixtures=true
            unit_property=true
            ;;
        # Standard and NPL libraries affect all Scheme suites, not C-only runtime tests.
        lib/*)
            libraries=true
            compiler=true
            toolchain=true
            repl=true
            fixtures=true
            unit_property=true
            ;;
        src/repl/*)
            repl=true
            fixtures=true
            toolchain=true
            ;;
        src/evaluator/*)
            repl=true
            fixtures=true
            toolchain=true
            libraries=true
            ;;
        tests/fixtures/core-bindings/*)
            fixtures=true
            repl=true
            libraries=true
            ;;
        tests/fixtures/*)
            fixtures=true
            libraries=true
            ;;
        tests/unit/*)
            unit_property=true
            compiler=true
            libraries=true
            ;;
        tests/property/*)
            unit_property=true
            libraries=true
            ;;
        tests/numeric/*)
            runtime=true
            ;;
        tools/libraries/*|tools/libraries.sld|tools/libraries-tests.npl|tools/export-origins.npl)
            libraries=true
            ;;
        tools/check_full_compiler.sh)
            compiler=true
            ;;
        tools/check_relay_scripts.sh|tools/check_current_fixed_point.sh|\
        tools/update_current_ssa.sh|tools/link-modes.sld)
            toolchain=true
            ;;
        tools/repl-suites.sld)
            repl=true
            ;;
        tools/expect-runner.npl|tools/run_r7rs_fixture.sh)
            fixtures=true
            repl=true
            ;;
        # Unknown path: fail-safe to full verification.
        *)
            ast_lint=true
            enable_all_suites
            ;;
    esac
}

mode=${1:-stdin}
case "$mode" in
    --all)
        ast_lint=true
        enable_all_suites
        emit_outputs
        ;;
    --base)
        if [ $# -lt 2 ]; then
            echo "usage: ci_changes.sh --base <git-ref>" >&2
            exit 2
        fi
        base=$2
        count=0
        while IFS= read -r file; do
            classify_path "$file"
            count=$((count + 1))
        done < <(git diff --name-only "$base...HEAD")
        if [ "$count" -eq 0 ]; then
            ast_lint=true
            enable_all_suites
        fi
        emit_outputs
        ;;
    stdin)
        while IFS= read -r file; do
            classify_path "$file"
        done
        emit_outputs
        ;;
    *)
        echo "usage: ci_changes.sh [--all | --base <git-ref>]" >&2
        exit 2
        ;;
esac
