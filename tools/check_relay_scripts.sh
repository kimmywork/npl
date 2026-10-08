#!/bin/bash
# Exercise relay orchestration with tiny deterministic host-tool substitutes.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
ACTIVE_PID=""
cleanup() {
    if [ -n "$ACTIVE_PID" ]; then
        kill -TERM "$ACTIVE_PID" 2>/dev/null || true
        wait "$ACTIVE_PID" 2>/dev/null || true
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
        echo "$!" > "$RELAY_TEST_CHILD"
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
bash "$WORK/tools/update_current_ssa.sh" > "$WORK/pass.log" 2>&1
grep -Fx canonical "$WORK/bootstrap/current.ssa" >/dev/null
grep -F 'current compiler fixed point: PASS' "$WORK/pass.log" >/dev/null

for mode in fail empty drift; do
    export RELAY_TEST_MODE=$mode
    status=0
    bash "$WORK/tools/update_current_ssa.sh" > "$WORK/fail.log" 2>&1 || status=$?
    [ "$status" -ne 0 ]
    grep -Fx canonical "$WORK/bootstrap/current.ssa" >/dev/null
    artifacts=$(awk '/incomplete SSA update; artifacts:/ {print $NF}' "$WORK/fail.log")
    [ -s "$artifacts/current-compiler" ]
    rm -rf "$artifacts"
done

export RELAY_TEST_MODE=stall
bash "$WORK/tools/update_current_ssa.sh" > "$WORK/cancel.log" 2>&1 &
ACTIVE_PID=$!
for ((i=0; i<100; i++)); do
    [ ! -s "$RELAY_TEST_CHILD" ] || break
    sleep 0.05
done
[ -s "$RELAY_TEST_CHILD" ]
child=$(< "$RELAY_TEST_CHILD")
kill -TERM "$ACTIVE_PID"
status=0
wait "$ACTIVE_PID" || status=$?
ACTIVE_PID=""
[ "$status" -eq 143 ]
for ((i=0; i<100; i++)); do
    if ! kill -0 "$child" 2>/dev/null; then break; fi
    sleep 0.05
done
if kill -0 "$child" 2>/dev/null; then
    kill -TERM "$child"
    echo 'relay cancellation left a child alive' >&2
    exit 1
fi
grep -Fx canonical "$WORK/bootstrap/current.ssa" >/dev/null
artifacts=$(awk '/incomplete SSA update; artifacts:/ {print $NF}' "$WORK/cancel.log")
[ -s "$artifacts/current-compiler" ]
rm -rf "$artifacts"
echo 'relay scripts: PASS (success, failed/empty/drifting emission, cancellation, seed preservation)'
