# AGENTS.md — NPL Project Briefing

> 本文件在每个 agent 会话开始时加载。保持简洁、可操作。

## 项目是什么

NPL：用 QBE 编译到 native 的 Scheme 方言。R7RS-small 兼容核心 + 渐进式类型系统（Typed-Racket 风格）+ 自举 + 神经网络语言构造。详见 `DESIGN.md`。

当前状态：**Phase 8 进行中**。Phase 0-7 全部完成（43 项测试通过）。正在实现 NPL 子集编译器用于自举。

## 工作约定

### 构建
```bash
cmake -B build -G Ninja          # 一次
ninja -C build qbe_build         # 构建 QBE
ninja -C build smoke && ./build/smoke/hello   # 冒烟测试
```
- 顶层 CMake 在仓库根。子项目：`tools/`（已建）、`stage0/`、`runtime/`、`tests/`（待建）。
- 工具链：CMake ≥3.20 + Ninja + Apple clang 17 + QBE（`external/qbe-1.3`，内置）。
- 目标架构默认 arm64（Apple Silicon）。

### 代码与组织
- stage0 宿主编译器用 **C99**（与 QBE、cproc 一致）。C 文件 `-std=c99 -Wall -Wextra -Wpedantic`。
- NPL 源文件在 `src/`，用 `.npl` 扩展名。
- 运行时 C 文件在 `runtime/`，被 stage0 和自举产物共同链接。
- QBE IL 文件用 `.ssa` 扩展名。
- 原则：YAGNI + KISS + Gall's Law（从简单系统演进）。先 MVP 后迭代，不预先添加扩展点。

### 测试与验证
- 每个增量必须跑验收命令并贴出输出作为证据。未贴证据不算完成（见 `PLAN.md` 约定）。
- 冒烟测试：`ninja -C build smoke && ./build/smoke/hello`。
- 自举验证（待建）：`tools/check_bootstrap.sh`，要求 stage1==stage2 字节一致。

### 变更与提交
- 提交约定：Conventional Commits（`feat`/`fix`/`docs`/`test`/`refactor`/`chore`）。
- 完成逻辑变更后记 worklog（见 `skill:worklog`）。
- 评审前用 `skill:review` 检查 diff。

## 关键约束（来自用户决策，勿擅改）

- GC：stop-the-world mark-sweep，自实现。
- stage0：Apple clang + C99。
- NN：静态图 DSL + 一等张量，分层。
- GPU：v1 仅 CPU SIMD（NEON/AVX），GPU/MPS 后置到 v2。
- minimal：不直接依赖 libc runtime。

## 关键技术映射（速查）

| 需求 | QBE 机制 | 证据 |
|------|----------|------|
| 闭包 | `env %e` 参数（函数首参） | `external/qbe-1.3/doc/il.txt` ~ Functions |
| 非 SSA 友好 | QBE 能编译非 SSA 代码 | `doc/il.txt` ~ Phi 节 |
| 全局数据 | `data $foo = { ... }` | `doc/il.txt` ~ Data 节 |
| record | `type :foo = { l, ... }`（传指针） | `doc/il.txt` ~ Aggregate Types |
| 线程局部 | `thread` linkage | `doc/il.txt` ~ Linkage 节 |
| 栈分配 | `alloc4/8/16` | `doc/il.txt` ~ Memory 节 |
| QBE 操作全集 | arith/mem/cmp/conv/cast/call/phi/jmp | `external/qbe-1.3/ops.h` |
| C→QBE 前端参考 | minic（951 行） | `external/qbe-1.3/minic/minic.y` |

## 参考实现

- **QBE**：后端，内置。`external/qbe-1.3/`。
- **cproc**：C→QBE 自举先例。自举 stage1/2/3 字节一致验证。
- **Chez Scheme / Cyclone / Loko / Ribbit**：Scheme native 自举先例。
- **Typed Racket**：渐进类型 + guard 模型。
- **Hare**：系统语言 + QBE 后端。

## 当前优先级

按 `PLAN.md` 推进。当前 Phase 0（工具链闭环）已验证。下一步：Phase 1（stage0 reader/parser 骨架）。
