# NPL 参考文档索引

本目录保存 NPL 使用的 R7RS 报告与 SRFI 规范副本。NPL 的实现采用
`(npl ...)` 规范库名，`(srfi N)` 是兼容别名；别名库只从对应的
`lib/npl/...` 库重新导出，不复制实现。

- `✅ 已实现`：仓库中存在可导入的 `(srfi N)` 库，或语言核心已实现对应语义。
- **规范库**：真正拥有实现的 `(npl ...)` 库；括号内为 `lib/npl` 下的文件。
- 未标记的文档只表示规范副本已收录，**不表示已经实现**。
- 规划部分只保留当前设计和实现需要的规范；无关的 docket 参考副本不长期保留。

## 快速入口

- [R7RS-small 报告](r7rs.html) · [NPL R7RS 一致性说明](../r7rs-conformance.md)
- [已实现的 SRFI](#已实现的-srfi)
- [语言层扩展](#语言层扩展已实现)
- [规划与参考 SRFI](#规划与参考-srfi)
- [既有未实现的 SRFI](#既有未实现的-srfi)

## 已实现的 SRFI

| SRFI | 规范 | 规范库（`lib/npl`） | 说明 |
|---:|---|---|---|
| **1** | ✅ [List Library](srfi-1.html) | `(npl list)`（`lib/npl/list.sld`） | 列表库 |
| **13** | ✅ [String Libraries](srfi-13.html) | `(npl string)`（`lib/npl/string.sld`） | 字符串库 |
| **18** | ✅ [Multithreading support](srfi-18.html) | `(npl thread)`（`lib/npl/thread.sld`） | 1:1 pthread 线程、互斥量、条件变量 |
| **19** | ✅ [Time Data Types and Procedures](srfi-19.html) | `(npl time)`（`lib/npl/time.sld`） | 时间与日期 |
| **41** | ✅ [Streams](srfi-41.html) | `(npl data stream)`（`lib/npl/data/stream.sld`） | 惰性流 |
| **44** | ✅ [Collections](srfi-44.html) | `(npl data collection)`（`lib/npl/data/collection.sld`） | 集合协议 |
| **47** | ✅ [Array](srfi-47.html) | `(npl data array)`（`lib/npl/data/array.sld`） | 数组，含 `#nA` 字面量 |
| **64** | ✅ [A Scheme API for test suites](srfi-64.html) | `(npl test)`（`lib/npl/test.sld`） | 测试框架 |
| **95** | ✅ [Sorting and Merging](srfi-95.html) | `(npl sort)`（`lib/npl/sort.sld`） | 与 SRFI 132 共享实现 |
| **111** | ✅ [Boxes](srfi-111.html) | `(npl data box)`（`lib/npl/data/box.sld`） | 单值箱 |
| **113** | ✅ [Sets and Bags](srfi-113.html) | `(npl data set)`（`lib/npl/data/set.sld`）<br>`(npl data bag)`（`lib/npl/data/bag.sld`） | 集合与包 |
| **125** | ✅ [Intermediate hash tables](srfi-125.html) | `(npl hashtable)`（`lib/npl/hashtable.sld`） | 哈希表 |
| **128** | ✅ [Comparators (reduced)](srfi-128.html) | `(npl comparator)`（`lib/npl/comparator.sld`） | 比较器 |
| **130** | ✅ [Cursor-based string library](srfi-130.html) | `(npl string cursor)`（`lib/npl/string/cursor.sld`） | 游标式字符串 |
| **132** | ✅ [Sort Libraries](srfi-132.html) | `(npl sort)`（`lib/npl/sort.sld`） | 与 SRFI 95 共享实现 |
| **133** | ✅ [Vector Library](srfi-133.html) | `(npl vector)`（`lib/npl/vector.sld`） | 向量库 |
| **140** | ✅ [Immutable Strings](srfi-140.html) | `(npl string immutable)`（`lib/npl/string/immutable.sld`） | 不可变字符串 |
| **146** | ✅ [Mappings](srfi-146.html) | `(npl data map)`（`lib/npl/data/map.sld`） | 映射 |
| **149** | ✅ [Basic Syntax-rules Template Extensions](srfi-149.html) | 无独立实现库（语言核心） | `syntax-rules` 已实现模板扩展；`(srfi 149)` 为特性检测库 |
| **158** | ✅ [Generators and Accumulators](srfi-158.html) | `(npl data generator)`（`lib/npl/data/generator.sld`） | 生成器与累加器 |
| **161** | ✅ [Unifiable Boxes](srfi-161.html) | `(npl data unifiable-box)`（`lib/npl/data/unifiable-box.sld`） | 可合一箱 |
| **170** | ✅ [POSIX API](srfi-170.html) | `(npl unix)`（`lib/npl/unix.sld`） | POSIX 接口 |
| **178** | ✅ [Bitvector library](srfi-178.html) | `(npl data bitvector)`（`lib/npl/data/bitvector.sld`） | 位向量 |
| **189** | ✅ [Maybe and Either](srfi-189.html) | `(npl data option)`（`lib/npl/data/option.sld`） | 完整接口；`(npl data maybe)` / `(npl data either)` 是两个子集 |
| **195** | ✅ [Multiple-value boxes](srfi-195.html) | `(npl data multiple-value-box)`（`lib/npl/data/multiple-value-box.sld`） | 多值箱 |
| **196** | ✅ [Range Objects](srfi-196.html) | `(npl data range)`（`lib/npl/data/range.sld`） | 区间对象 |
| **252** | ✅ [Property Testing](srfi-252.html) | `(npl test property)`（`lib/npl/test/property.sld`） | 属性测试 |

所有上表条目均可使用对应的 `(srfi N)` 别名导入，例如：

```scheme
(import (npl list) (npl data range))  ; 规范库名
(import (srfi 1) (srfi 196))          ; 兼容别名
```

## 语言层扩展（已实现）

| SRFI | 规范 | 实现位置 | 说明 |
|---:|---|---|---|
| **46** | ✅ [Basic Syntax-rules Extensions](srfi-46.html) | 语言核心的 `syntax-rules` | 支持尾模式与自定义省略号；通过 `cond-expand` 特性 `srfi-46` 检测，没有独立的 `lib/npl` 库 |

SRFI 149 的模板扩展同样在语言核心中实现，并提供 `srfi-149`、
`srfi-149-compatible` 特性标识；其兼容库 `(srfi 149)` 仅用于特性检测，
见[上表](#已实现的-srfi)。

## 规划与参考 SRFI

以下规范副本用于后续设计和实现，**不表示已经实现或承诺实现**。
分类按 WG2 Dockets；只保留当前规划需要的条目，未列入的 docket 提案
在真正需要时再补充。

### Red / Tangerine（已投票核心）

**Red：数据结构**

- [SRFI 14: Character-set Library](srfi-14.html)
- [SRFI 117: Queues based on lists](srfi-117.html)
- [SRFI 134: Immutable Deques](srfi-134.html)

**Tangerine：数值与文本**

- [SRFI 115: Scheme Regular Expressions](srfi-115.html)
- [SRFI 141: Integer division](srfi-141.html)
- [SRFI 143: Fixnums](srfi-143.html)
- [SRFI 144: Flonums](srfi-144.html)
- [SRFI 151: Bitwise Operations](srfi-151.html)
- [SRFI 159: Combinator Formatting](srfi-159.html)
- [SRFI 160: Homogeneous numeric vector libraries](srfi-160.html)

### Hypnos / Orange（可移植应用库）

- [SRFI 48: Intermediate Format Strings](srfi-48.html)
- [SRFI 173: Hooks](srfi-173.html)
- [SRFI 175: ASCII character library](srfi-175.html)
- [SRFI 180: JSON](srfi-180.html)
- [SRFI 207: String-notated bytevectors](srfi-207.html)
- [SRFI 209: Enums and Enum Sets](srfi-209.html)
- [SRFI 214: Flexvectors](srfi-214.html)

### 语法与易用性

- [SRFI 2: AND-LET*](srfi-2.html)
- [SRFI 26: Notation for Specializing Parameters without Currying](srfi-26.html)

### Runtime / I/O

- [SRFI 106: Basic socket interface](srfi-106.html)
- [SRFI 181: Custom ports (including transcoded ports)](srfi-181.html)
- [SRFI 192: Port Positioning](srfi-192.html)

### CLI / 配置 / 应用

- [SRFI 37: args-fold: a program argument processor](srfi-37.html)
- [SRFI 177: Portable keyword arguments](srfi-177.html)
- [SRFI 193: Command line](srfi-193.html)
- [SRFI 233: INI files](srfi-233.html)

### 独立新标准

- [SRFI 250: Insertion-ordered hash tables](srfi-250.html)
- [SRFI 275: URIs, IRIs, and basic paths](srfi-275.html)
- [SRFI 281: Bytevector Utilities](srfi-281.html)

## 既有未实现的 SRFI

以下规范副本在本轮扩展前已收录，但仓库目前没有对应的 `(srfi N)` 库。
部分功能可能由其他库或语言核心覆盖，已在“现状”中标明。

| SRFI | 规范 | 现状 |
|---:|---|---|
| **23** | [Error reporting mechanism](srfi-23.html) | 未提供 `(srfi 23)`；`error`、`error-object?` 等已在 `(scheme base)` 中 |
| **27** | [Sources of Random Bits](srfi-27.html) | 未提供；`(npl test property)` 内部的随机源不是 SRFI 27 公共 API |
| **42** | [Eager Comprehensions](srfi-42.html) | 未提供 |
| **78** | [Lightweight testing](srfi-78.html) | 未提供；测试请使用 `(npl test)` / `(srfi 64)` |
| **121** | [Generators](srfi-121.html) | 未提供 `(srfi 121)` 别名；生成器 API 由 SRFI 158 扩展覆盖，见 `(npl data generator)` / `(srfi 158)` |
| **145** | [Assumptions](srfi-145.html) | 未提供公开库；部分实现内部使用私有的 `assume` 辅助函数 |
| **194** | [Random data generators](srfi-194.html) | 未提供 |
| **226** | [Control Features](srfi-226.html) | 未提供；仅具备 R7RS 及 NPL 自身的控制功能 |
| **230** | [Atomic Operations](srfi-230.html) | 未提供 |
| **269** | [Portable Test Definitions](srfi-269.html) | 未提供；测试使用 SRFI 64 / SRFI 252 对应的 `(npl test ...)` 库 |

## 说明

- 本目录的 HTML 文件是 SRFI 与 R7RS 的规范副本，版权归各自作者所有；
  它们不是 NPL 项目文档。
- 库的实现状态以 `lib/srfi/*.sld` 和 `lib/npl/**/*.sld` 为准；新增 SRFI
  时请同时更新本索引。
- 库命名与依赖分层规则见 [扩展 NPL](../extending.md)，测试与验收方式见
  [验证与门禁](../testing.md)。
