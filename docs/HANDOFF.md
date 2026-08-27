# NPL 项目 Handoff 文档

> 更新日期: 2026-08-27 | 分支: master | 最新 commit: `6d56a41`
> 上一个任务: Phase 8 自举闭环 + 增量1~3(R7RS 特性/测试基建)

---

## 一、当前状态总览

### 构建与测试(✅ 全部通过)

```bash
# 构建
cmake -B build -G Ninja && ninja -C build          # 全量构建
ninja -C build smoke && ./build/smoke/hello        # Phase 0 冒烟: "hello world"

# 测试(一次性跑全部)
./tools/run_tests.sh           # 五套件统一入口, exit 0 = 全绿
./tools/check_bootstrap.sh     # Phase 8 验收, exit 0 = 不动点达成
```

| 验收项 | 状态 | 证据 |
|--------|------|------|
| stage0 reader/ast + smoke | ✅ | `tools/check_stage0.sh` |
| R7RS conformance (10 组) | ✅ | `tools/check_r7rs.sh` |
| core features (increment-1/2) | ✅ | `tools/check_core.sh`(含 4 条编译期契约)|
| type system (10 组) | ✅ | `tools/check_type.sh` |
| property-based tests (PBT) | ✅ | `tools/prop_tests.py` — P1 eval 正确性(200-seed)/P2 fuzz/P3 fail-fast/P4 AST 稳定|
| **Phase 8 Stage A** | ✅ | nplc1 编译算术=5/递归=7/add(20,22)=42/'ok'+exit9 |
| **Phase 8 Stage B 不动点** | ✅ | il2==il3 **6267 行逐字节一致**(cproc 式验证)|

### 交付物清单
- `src/compiler.npl` — 自举编译器(L1 语言面, ~930 行)
- `runtime/simd_kernels.c/.h + simd_kernels_scalar.c` — NEON kernels(add/mul/dot + in-place add)
- `runtime/simd_bench.c` + `tools/check_simd.sh` — 正确性 + ≥3x 加速验收
- `docs/r7rs-audit.md` — R7RS-small 全库合规审计(319 标识符机械化盘点)
- `docs/reference/r7rs.html` — 官方 HTML(vendored)
- `docs/design-inc1-core.md` + `docs/worklog/2026-08-27-phase8.md` — 设计决策与诊断日志

---

## 二、遇到的问题(全部已解决或已定位)

### 已解决的关键缺陷(按严重程度排序)
1. **call-with-ops 参数硬编码上限 3 个** —— >=4 参的调用在自举 driver 与所有产物中被静默截断。
   这是 Stage-B 崩溃的根因;修复为 args-join 折叠任意长度(`ca358ab`)。**教训:凡"参数拼接"
   类逻辑必须用列表折叠而非嵌套 if 分发。**
2. **跨块 temp 误编译** —— if 双分支各自给同一 temp 赋值、汇合块读取,QBE 在此模式产生错误代码;
   按 doc/il.txt 指引改为栈槽传递(storel/loadl)。Stage-0 与自举产物同受影响。
3. **retag bool 公式错误** —— #t/#f 的 imm 编码 `{nil=2,#t=6,#f=10}` 非仿射,
   原 `shl+add` 会把比较结果错误映射到 nil 区间;修复为 `sub(#FALSE_BASE, shl(raw))`。
4. **peek 层次错位** —— cur 是 cell→token-list→token→field,**三层 car** 才能取到 kind/payload。
5. **AST tag 常量漂移** —— parse 侧与 fun-def? 判定的 tag 数字不一致(letrec 用 211);
   统一后消除"符号变成布尔值"的诡异崩源。
6. **sanitize 死链** —— lower-case 分支条件永假且 `(int->string c)` 不可达;
   函数名被 sanitize 成空串 → `$__` 这类函数名 → FUNCS 表损坏。
7. **emit-main 漏 `$npl_global_inits()` 调用** —— 含 runtime-init 全局的程序其数据槽保持 nil。

### 未解决的问题(下个会话优先处理)
1. **letrec 解析偏移 bug(wip, stash 中)** ——
   `(letrec ((a 40)) a)` 触发 "unexpected )";
   工作副本在 stash (`git stash list` 可见 wip-letrec-letstar-partial),
   含 when/unless/case/do/letrec/let* 的 parser+emit 半成品与三个候选修复方向。
2. **npl_cdr/npl_car fail-fast 改动** —— 现在 runtime 对非 pair 输入直接 abort(exit 1),
   不再返回 nil;若有调用方依赖旧行为需排查(git log runtime/alloc.c 有记录)。
3. **diagnostic trace 钩子** —— npl_tracev/npl_trace2/npl_dump_addrs 留在 runtime
   (fprintf 到 stderr), 编译器主路径零影响, 后续可加 #ifdef 或删除。
4. **structured concurrency(spawn/await/scope)** —— 用户指示降级未实施(PLAN Phase 9 后半段)。

### 已知限制(设计层面接受)
- 自举语言面 L1 故意**不含闭包/高阶函数**调用(limiting but deliberate),
  compiler.npl 本体只消费 top-level define + 内建;
- 无 string->symbol(缺全局 symbol interning 表贯穿静态 quote 与 runtime box);
- bytevector/port 完整库未实现。

---

## 三、接下来的规划

### P0(下一会话最优先)
1. **恢复 stash 并落地 letrec 偏移修复**: binding-pairs 需要"外层负责吃掉 bindings 收尾括号"
   或统一由 caller `expect-rparen`;参考文档的候选方案A/B/C逐一试;
2. **完成 case/do 的 emit 分支**(tag 9/10 已经 parse 但尚未实现 dispatch)
3. **把 /tmp/w2x 内零散测试正式化** → 合入 tools/check_core.sh + tests/

### P1(Phase 9 推进)
4. structured concurrency: spawn/await/scope(runtime pthread shim +
   work-stealing 简版), PLAN.md 原文要求
5. NEON: matmul kernel(PLAN 说 SIMD 3x+ 达标, 可继续扩展 NN 需要的算子)

### P2(R7RS 缺口收敛, 详见 docs/r7rs-audit.md 优先级)
6. C 组宏派生(when/unless/do/case——宏挂接进自举面)
7. E 组(letrec 家族), D(quasiquote 简化版)
8. F 组(list/map/reverse 等 NPL 层可写的 library function)
9. 数值塔(abs/floor/gcd/lcm 等), 异常族(error/raise/guard; 先做 escape-only call/cc
   即满足 DESIGN v1 承诺), G(define-library 等大特性最后)

### 验收口径提醒(团队约定)
- 每次提交前跑 `./tools/run_tests.sh`(全绿)+ `./tools/check_bootstrap.sh`(exit 0)
- 编译器改动必须同时更新 design doc 的已知限制与 changelog
- 所有引用是**基于 docs/reference/r7rs.html** 机械抽取的结果, 切换标准时必须重新生成
