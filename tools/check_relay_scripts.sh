#!/bin/bash
# Exercise relay orchestration with tiny deterministic host-tool substitutes.
set -eu
fail() {
    printf 'relay scripts: FAIL: %s\n' "$1" >&2
    exit 1
}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
ACTIVE_PID=""
cleanup() {
    if [ -n "$ACTIVE_PID" ]; then
        kill -TERM "$ACTIVE_PID" 2>/dev/null || true
        wait "$ACTIVE_PID" 2>/dev/null || true
    fi
    if [ -n "${RELAY_TEST_CHILD:-}" ] && [ -s "$RELAY_TEST_CHILD" ]; then
        sleeper=$(< "$RELAY_TEST_CHILD")
        kill -TERM "$sleeper" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT
mkdir -p "$WORK/tools" "$WORK/bin" "$WORK/build/qbe" "$WORK/bootstrap"
cp "$ROOT/tools/check_current_fixed_point.sh" "$WORK/tools/"
cp "$ROOT/tools/update_current_ssa.sh" "$WORK/tools/"
printf 'seed\n' > "$WORK/bootstrap/current.ssa"
cat > "$WORK/bin/cmake" <<'STUB'
#!/bin/bash
exit 0
STUB
cp "$WORK/bin/cmake" "$WORK/bin/ninja"
cat > "$WORK/build/qbe/qbe" <<'STUB'
#!/bin/bash
cp "$3" "$2"
STUB
cat > "$WORK/bin/cc" <<'STUB'
#!/bin/bash
cp "$STUB_COMPILER" "$2"
STUB
cat > "$WORK/compiler" <<'STUB'
#!/bin/bash
case "$RELAY_TEST_MODE" in
    fail) exit 7 ;;
    empty) exit 0 ;;
    drift) printf '%s\n' "$0"; exit 0 ;;
    stall)
        sleep 300 &
        sleeper=$!
        # Install the handler before publishing the child pid: the harness
        # cancels as soon as the file appears.
        trap 'kill -TERM "$sleeper" 2>/dev/null || true; wait "$sleeper" 2>/dev/null || true; exit 143' TERM
        echo "$sleeper" > "$RELAY_TEST_CHILD"
        wait
        ;;
    *) printf 'canonical\n'; exit 0 ;;
esac
STUB
chmod +x "$WORK/bin/"* "$WORK/build/qbe/qbe" "$WORK/compiler"
export PATH="$WORK/bin:$PATH"
export STUB_COMPILER="$WORK/compiler"
export RELAY_TEST_CHILD="$WORK/sleeper.pid"
export RELAY_TEST_MODE=pass
bash "$WORK/tools/update_current_ssa.sh" > "$WORK/pass.log" 2>&1 || fail 'passing relay failed'
grep -Fx canonical "$WORK/bootstrap/current.ssa" >/dev/null || fail 'passing relay did not install the seed'
grep -F 'current compiler fixed point: PASS' "$WORK/pass.log" >/dev/null ||
    fail 'passing relay did not report the fixed point'

for mode in fail empty drift; do
    export RELAY_TEST_MODE=$mode
    status=0
    bash "$WORK/tools/update_current_ssa.sh" > "$WORK/fail.log" 2>&1 || status=$?
    [ "$status" -ne 0 ] || fail "$mode emission unexpectedly succeeded"
    grep -Fx canonical "$WORK/bootstrap/current.ssa" >/dev/null ||
        fail "$mode emission overwrote the seed"
    artifacts=$(awk '/incomplete SSA update; artifacts:/ {print $NF}' "$WORK/fail.log")
    [ -n "$artifacts" ] || fail "$mode emission reported no artifacts"
    [ -s "$artifacts/current-compiler" ] || fail "$mode emission lost its compiler"
    rm -rf "$artifacts"
done

export RELAY_TEST_MODE=stall
bash "$WORK/tools/update_current_ssa.sh" > "$WORK/cancel.log" 2>&1 &
ACTIVE_PID=$!
for ((i=0; i<100; i++)); do
    [ ! -s "$RELAY_TEST_CHILD" ] || break
    sleep 0.05
done
[ -s "$RELAY_TEST_CHILD" ] || fail 'stalled emission published no child pid'
child=$(< "$RELAY_TEST_CHILD")
kill -TERM "$ACTIVE_PID" || fail 'relay exited before cancellation'
status=0
wait "$ACTIVE_PID" || status=$?
ACTIVE_PID=""
[ "$status" -eq 143 ] || fail "cancellation exited with $status, expected 143"
for ((i=0; i<100; i++)); do
    if ! kill -0 "$child" 2>/dev/null; then break; fi
    sleep 0.05
done
if kill -0 "$child" 2>/dev/null; then
    kill -TERM "$child"
    fail 'relay cancellation left a child alive'
fi
grep -Fx canonical "$WORK/bootstrap/current.ssa" >/dev/null ||
    fail 'cancellation overwrote the seed'
artifacts=$(awk '/incomplete SSA update; artifacts:/ {print $NF}' "$WORK/cancel.log")
[ -n "$artifacts" ] || fail 'cancellation reported no artifacts'
[ -s "$artifacts/current-compiler" ] || fail 'cancellation lost its compiler'
rm -rf "$artifacts"
echo 'relay scripts: PASS (success, failed/empty/drifting emission, cancellation, seed preservation)'
