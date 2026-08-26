# NPL — A Typed, Native Scheme with Neural-Network Constructs

NPL 是一个用 QBE 编译到 native 的 Scheme 方言，目标是 R7RS-small 兼容核心 + 渐进式类型系统 + 自举 + 神经网络语言构造。

## 状态

**设计/骨架阶段** — 设计文档与增量计划已完成，项目骨架已搭建，QBE 工具链闭环已验证（冒烟测试通过）。

## 技术栈

- **语言核心**：R7RS-small 子集 + Typed-Racket 风格类型注解 + Hindley-Milner 推导
- **后端**：QBE 1.3（`external/qbe-1.3`，已内置）
- **目标架构**：arm64（Apple Silicon 优先）、amd64、rv64
- **宿主编译器**：Apple clang 17（C99）
- **构建**：CMake ≥ 3.20 + Ninja
- **运行时**：自实现 stop-the-world mark-sweep GC，不直接依赖 libc

## 快速开始

```bash
# 构建（含 QBE）
cmake -B build -G Ninja && ninja -C build qbe_build

# 运行冒烟测试（验证工具链闭环）
ninja -C build smoke && ./build/smoke/hello
# 输出: hello world
```

## 文档

- [`DESIGN.md`](DESIGN.md) — 完整设计：语言规格、类型系统、编译器架构、QBE 映射、runtime、自举、库、并行化、多后端、NN
- [`PLAN.md`](PLAN.md) — 分阶段可验证增量执行计划（Phase 0–12）

## 项目结构

```
npl/
├── DESIGN.md / PLAN.md        # 设计与计划
├── CMakeLists.txt             # 顶层构建
├── external/qbe-1.3/          # QBE 后端（内置）
├── tools/                     # 冒烟测试（QBE IL 直通）
├── stage0/                    # C99 宿主编译器（自举起点，待建）
├── runtime/                   # 共享 runtime（GC/alloc，待建）
├── src/                       # NPL 自举源（待建）
└── tests/                     # 测试（待建）
```

## 设计要点

| 维度 | 决策 |
|------|------|
| GC | Stop-the-world mark-sweep（自实现） |
| 自举 stage0 | Apple clang + C99 |
| 闭包 | QBE 原生 `env` 参数 |
| 尾调用 | self-recursion→QBE `jmp`；跨函数→env trampoline |
| 类型 | HM + 注解，typed 代码 unboxed |
| 并行化 | v1: CPU SIMD (NEON/AVX)；GPU/MPS 后置 |
| NN | 静态图 DSL + 一等张量（分层） |

详见 `DESIGN.md`。
