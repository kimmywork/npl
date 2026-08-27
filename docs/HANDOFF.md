# NPL 项目 Handoff 文档

> 更新日期: 2026-08-28 | 分支: master | 基线 commit: `f4a626c`
> 上一个任务: reader delimiter diagnostics + let-family 修复 + self-hosted `case` 子集

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
| **Phase 8 Stage A** | ✅ | 基线 4 程序 + let-family 6 程序 + case 3 程序 + unmatched diagnostics 2 组 |
| **Phase 8 Stage B 不动点** | ✅ | il2==il3 **10742 行逐字节一致** |

> 当前待提交改动已通过 `run_tests.sh`、`check_bootstrap.sh` 与 smoke。`case`
> 仅承诺本轮覆盖的 datum-list/else/顺序 body 子集；完整 R7RS `case` 仍见
> `docs/r7rs-audit.md` 的限制说明。

### 交付物清单
- `src/compiler.npl` — 自举编译器(L1 语言面,含 let-family 与 `case` 子集)
- `tools/check_bootstrap.sh` — let-family/case/unmatched diagnostics 回归 + fixpoint
- `stage0/src/reader.c` + `tools/check_stage0.sh` — 匹配 delimiter 与 opening 位置诊断
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
8. **letrec/let* 解析偏移 bug** —— 根因是 `parse-let` 会预吃
   bindings 外层 `(`, 而 `parse-special` 的 letrec/let* 路径不预吃, 共用同一 `binding-pairs`
   导致错位 → `(letrec ((a 40)) a)` 报 "unexpected )"/"unbound symbol 'a'"。
   修复: `binding-pairs` 统一负责吃外层 `(` + 收尾 `)`, `parse-let` 不再预吃;
   另修 `tagged-dispatch` 的 tag 11/13 → 220/221(parse 产出与 dispatch 检查不一致)。
   验证: bootstrap Stage A 新增 6 个回归测试(p_let/p_let2/p_letrec/p_letrec2/p_letstar/
   p_letstar2)全过。
9. **case 顶层结构错位** —— `emit-case-chain` 少一个右括号、`emit-datum-tests`
   多一个右括号，总深度为 0 但两个后续 `define` 被吞进函数体。修复函数边界后，补齐
   datum literal、else、顺序 body 与 no-match 初始化路径；3 个 Stage A 回归通过。
10. **reader delimiter 诊断不足** —— stage0 曾静默接受 `(]`，且 token 行列记录在跳过空白前；
    现按 opening token 匹配 `(`/`)`、`[`/`]` 并报告 opening 位置。nplc1 在 tokenization 前
    分块扫描字符串/注释之外的括号，长源码不会因逐字符递归耗尽栈。

### 未解决的问题(下个会话优先处理)
1. **R7RS `case` 仍是子集** —— 已支持数值/symbol datum list、else 与多表达式 body；
   尚未支持标准中的 `=>` clause，quoted datum 类型仍受自举 reader 限制。
2. **letrec 语义仍是近似** —— 当前 emitter 复用顺序 `emit-binds`，满足本轮值绑定回归，
   但不提供完整 letrec 的预分配 location/相互递归初始化语义。
3. **npl_cdr/npl_car fail-fast 改动** —— 现在 runtime 对非 pair 输入直接 abort(exit 1),
   不再返回 nil;若有调用方依赖旧行为需排查(git log runtime/alloc.c 有记录)。
4. **diagnostic trace 钩子** —— npl_tracev/npl_trace2/npl_dump_addrs 留在 runtime
   (fprintf 到 stderr), 编译器主路径零影响, 后续可加 #ifdef 或删除。
5. **structured concurrency(spawn/await/scope)** —— 用户指示降级未实施(PLAN Phase 9 后半段)。

### 已知限制(设计层面接受)
- 自举语言面 L1 故意**不含闭包/高阶函数**调用(limiting but deliberate),
  compiler.npl 本体只消费 top-level define + 内建;
- 无 string->symbol(缺全局 symbol interning 表贯穿静态 quote 与 runtime box);
- bytevector/port 完整库未实现。

---

## 三、接下来的规划

### P0(下一会话最优先)
1. **完成 Phase 5 剩余类型能力**：record/row/refinement 与更多复合类型 lowering，
   并把局部 HM/typed lambda/结构类型子集移植到 self-host compiler。
2. **补齐 Phase 7**：base library、vector、flonum、bignum 与 `100!`。

### P1(R7RS 缺口收敛, 详见 docs/r7rs-audit.md 优先级)
3. C 组剩余派生形式(`do`/`cond-expand`)与 `case =>`
4. E 组完整 letrec 语义,D(quasiquote 简化版)
5. F 组(list/map/reverse 等 NPL 层可写的 library function)
6. 数值塔(abs/floor/gcd/lcm 等)与异常族(error/raise/guard)，
   G(define-library 等大特性最后)

### 验收口径提醒(团队约定)
- 每次提交前跑 `./tools/run_tests.sh`(全绿)+ `./tools/check_bootstrap.sh`(exit 0)
- 编译器改动必须同时更新 design doc 的已知限制与 changelog
- 所有引用是**基于 docs/reference/r7rs.html** 机械抽取的结果, 切换标准时必须重新生成
