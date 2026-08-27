# 增量 1 设计 — 核心语言特性补齐(GAP 修复)

> 状态:实施中 | 前置:Phase 0-7 基线全绿(check_stage0.sh / check_r7rs.sh 通过)

## 1. 背景:核查出的 GAP

对照 PLAN.md Phase 8 目标(stage1 编译 src/compiler.npl)逐项核查 stage0 能力,
发现以下 GAP(全部有复现命令佐证):

| # | GAP | 影响 | 证据 |
|---|-----|------|------|
| G1 | `(cond ...)`/`else` 未支持,被当作未定义函数调用 | 任何自举编译器源都过不了链接 | `cond ((= x 1) ...)` → link error `_cond`, `_else` |
| G2 | body 内部 define 未处理;lambda 多表达式 body 会走 application 分支 | `(lambda () (define x 1) (+ x 1))` 语义错乱 | qbe_gen.c gen_expr 无此分支 |
| G3 | `(quote datum)` 编译为字面 `0`(静默!) | self-host reader 无法判别 symbol;`(= op '+)` 类操作不可能 | ast 注释 + gen_expr QUOTE 返回 "0" |
| G4 | 顶层变量 `(define x 5)` 被静默丢弃("skip for now") | reader.npl 的 token 常量 define(TOK_LPAREN 0)全部失效 | qbe_gen_program 第一遍 |
| G5 | unsupported 静默返回 "0"(set!/quote/default) | 错误程序产出静默错误的二进制,fail-fast 缺失 | default 分支 |
| G6 | immediate 编码三处互相矛盾:npl.h(#t=6/#f=10/nil=2)vs gen_literal(4/8/0)vs if(jnz 非 0 即真) | `#f` 为真;数值塔/eq?/display 全部建立在不一致的沙上 | gen_literal L166 "4"/"8";npl.h IMM_* |
| G7 | 缺谓词:not / eq? / pair?(null? 有) | R7RS 核心不可用 | builtin 表 |
| G8 | display 不能打印 symbol/bool/nil 全集 | 调试产物不可读 | runtime/alloc.c |
| G9 | 测试基建:check_*.sh 全部用 exit%256 黑盒,无可读断言,无契约/PBT | 回归网太粗,新特性验证不足 | 各 check 脚本 |

G9 由单独增量解决;本增量修 G1-G8。

## 2. 方案设计

### 2.1 唯一事实来源:value 编码(runtime/npl.h)

```
NplVal (64-bit tagged):
  fixnum : v << 2 | 0        (最高位丢弃 → 31-bit 有序 fixnum 有效位)
  boxed  : ptr | 1           (NplHeader{tag32, flags32} + payload)
  imm    : enc << 2 | 2      nil=2  #t=6  #f=10
```

修复原则:**所有通道(gen_literal / cmp 结果 / if 判定 / eq? / display)
向该编码对齐**,而不是向 stage0 里的历史误编码对齐。

### 2.2 truthiness(R7RS 严格化 + 兼容路径)

- `if` 判定改为 `%c =w cnew %test, 10; jnz %c, ...` — 只有 `#f` 为假。
- 同步:cmp op(`< > = <= >= not=`)的结果从裸 0/1 **re-tag 为 #t/#f**
  (`shl 2; add 2`)。裸 0 不再是合法布尔,falsy 判定因此天然成立。
- `null?` 改判 `ceqw %x, 2`(nil=2)。
- 已知取舍:fixnum 0(v=0)恰好也是 falsy 常量(`cnew` 对 v=0 产生 #t),
  即 `(if 0 ...)` 会误判为假 — 违背 R7RS 但影响面极小(比较结果才是
  主流测试值),记为 KNOWN-LIMITATION,V2 用类型系统消除(typed 分支
  直接 jnz)。
- and/or 展开的中间值仍是真值表达式,不受影响。

### 2.3 cond/else — desugar pass(非 special form)

新增 `stage0/src/desugar.{c,h}`:在 macro 展开**之后**跑一次纯 AST 转换:

```
(cond (t1 e...) (t2 e...) ... (else e...))
→ (if t1 (begin e...) (if t2 (begin e...) ... (begin else...)))
```

- clause body 为空(test-only)时,值为 test 本身(R7RS 行为)。
- else 不在末尾 / else 重复 → 编译期报错。
- 选择 desugar 而非 syntax-rules:阶段定位上 cond 属核表(driver 直译),
  且 compiler.npl 未来照抄同一逻辑复杂度最低(KISS,Gall's Law)。

### 2.4 内部 define(letrec* 近似)+ 多表达式 body

qbe_gen 新增统一入口 `gen_body(ctx, exprs)`:

```
对 body 逐项:
  (define v e) → 先 symtab_add_local(v)(名字先入域,letrec* 语序可见性)
                 再 gen value,copy 入槽
  其他         → gen_expr(最后一个的值作为 body 值)
```

- lambda / let 的 body 全部改走 gen_body。
- KNOWN-LIMITATION:body 内 define 的名字不得被同一函数体内嵌 lambda
  捕获(collect_free_vars 将其并入 exclude);需要共享状态请显式传参。
  自举编译器遵守此规则即可。
- 同时根治多表达式 body 被 application 分支误吞的 bug(G2b)。

### 2.5 顶层全局变量(data 静态初始化)

`(define name const)` 在 qbe_gen_program 中生成 QBE data:

```
data $gv_name = { l <static-repr> }    ; repr 为一次性求值的静态形式
```

静态初始化支持:fixnum / bool / nil / string literal / `'symbol` /
`'(elements...)`(递归成对链)。其余表达式(含运算、调用)→ 编译期报错
(YAGNI:compiler.npl 只需常量绑定;运行时 store 写全局置 V2)。

访问:symbol 解析出 SYM_GLOBAL_VAR 时 `loadl $gv_name`。
符号表新增 `SYM_GLOBAL_VAR`。

### 2.6 quote 编译语义

gen_expr 新增 AST_QUOTE:按 datum 类型递归构造静态 data 对象,返回 `$label`:

| datum | data 形式 | NplVal |
|-------|-----------|--------|
| number | `{ l num }`(直接 fixnum,无 box) | v<<2 |
| bool/nil | 无需 data,立即数 | 6/10/2 |
| string | `{ w TAG_STRING, w 0, w len, w cap, b "..", b 0 }` | $label\|1 |
| symbol | `{ w BOX_SYMBOL, w 0, w len, w cap, b "..", b 0 }` | $label\|1 |
| pair | `{ w BOX_PAIR, w 0, l $car, l $cdr }` 递归 | $label\|1 |

- 相同 literal 复用同一 label(按符号名去重即可,symbol 天然按名字;
  pair 链不去重,YAGNI)。
- 这使 self-host reader 可以执行 `(= tok TOK_LPAREN)` 之外真正的
  `(eq? (car form) 'define)` 判别。

### 2.7 新内建谓词

| 形式 | 实现 |
|------|------|
| `(not x)` | `%r =w cnew %x, 10` → re-tag bool |
| `(eq? a b)` | `%r =l ceql %a, %b`(boxed/imm/fixnum 指针级相等;string 内容相等不在 eq? 承诺内,R7RS 合规) |
| `(pair? x)` | `%h =l loadsw %x`(取 header.tag) `%r =w ceqw %h, 0(BOX_PAIR)` — 仅当低 2 位 =1?简化:tagged ptr 必然低 2 位 01,直接比 header tag |

### 2.8 display 增强(runtime)

`npl_display` 补齐:BOX_SYMBOL(打印名字)/立即数(2→`()`,6→`#t`,
10→`#f`;旧误编码 4/8 打印 `#t/#f` 保过渡兼容)/STRING 全集已有。

### 2.9 fail-fast(错误卫生)

gen_expr default 分支、AST_SET 等未支持构造:**fprintf(stderr) + exit(1)**,
不再静默 "0"。compile() 收尾也校验 QBE/link 失败路径(已核对返回码=1 ✓)。

## 3. 测试计划

- 新增 `stage0/tests/test_core.npl`:cond 全形态(test-only body、else、
  嵌套)、内部 define、多表达式 body、全局常量、quote(symbol/string/
  nested list)、not/eq?/pair?、truthiness(#f 进 alt,nil 不假)。
- 新增 `tools/check_core.sh`:非黑盒断言(display 输出 diff 到 golden
  + exit code 检查),纳入回归。
- 契约不变量(随 inc3 并入正式框架,本增量先手跑):
  1. 任意 prog 走完 reader/desugar/codegen 三相,要么成功要么非零退出,
     绝不静默输出错误二进制。
  2. immediates 数据:round-trip 显示 #t/#f/()/quote-symbol 一致。
  3. `(if #f A B)` 恒 B;`(if '() A B)` 恒 A(R7RS:nil 真)。
- 全量回归:tools/check_stage0.sh + check_r7rs.sh + check_type.sh +
  ninja 冒烟,必须保持全绿(exit-code 断言已按 #f=10/re-tag 口径复核)。

## 4. 文件改动清单

| 文件 | 动作 |
|------|------|
| stage0/src/desugar.{c,h} | 新增 |
| stage0/src/qbe_gen.{c,h} | 大改(if/cmp/literal/quote/gvar/gen_body/pred/fail-fast) |
| stage0/src/symtab.{c,h} | SYM_GLOBAL_VAR |
| stage0/src/main.c | 挂 desugar pass |
| stage0/CMakeLists.txt | 加 desugar.c |
| runtime/{npl.h,alloc.c} | npl_display 补齐 |
| stage0/tests/test_core.npl, tools/check_core.sh | 新增 |

---

# 增量 2 设计 — 字符串与符号内建(自举编译器依赖)

## 1. GAP

compiler.npl 已在使用 `string-append`、`number->string`(reader 输出 QBE 代码
需要拼接字符串),但 stage0/runtime 均未提供。

## 2. 方案

新增 runtime 函数(C,跟随现有 malloc/box 模型),QBE builtin 分发 call:

| 形式 | runtime 函数 | 返回约定 |
|------|--------------|----------|
| `(string-append a b)` | `npl_string_append` | tagged NplVal(ptr\|1) |
| `(string-length s)` | `npl_string_length` | 裸 int64 → codegen `shl 2` 补 tag |
| `(string-ref s i)` | `npl_string_ref` | fixnum 字码(`c<<2`) |
| `(string=? a b)` | `npl_string_equal` | 裸 0/1 → retag_bool |
| `(number->string n)` | `npl_number_to_string` | tagged string box |
| `(symbol->string s)` | `npl_symbol_to_string` | 同布局,tag 换 BOX_STRING |
| `(string->symbol s)` | `npl_string_to_symbol` | 同布局,tag 换 BOX_SYMBOL |
| `(substring s start end)` | 暂缓(YAGNI,自举暂不需要) | — |

- v1 只支持 ASCII 单字节 string-ref(UTF-8 后置)。
- 复用 `emit_string_box` 的 box 布局 `{tag,len,cap,data}`;symbol↔string
  互换 = 分配新 box 拷贝字节、仅 header.tag 不同。
- 所有内建经统一 builtin 表分发;未实现内建继续 fail-fast。

## 3. 测试

`stage0/tests/test_strings.npl` + `tools/check_core.sh` 扩展一节:
round-trip(number->string ↔ display 输出一致)、append 长度守恒、
ref 边界、symbol/string 互转恒等(eq? 与 string=?)。

---

# 增量 4 设计 — 自举编译器(src/compiler.npl)

## 1. 形态与验收映射

- `nplc1` := nplc0 编译 src/compiler.npl 的产物(读 NPL 源 → 输出 QBE IL 到 stdout)。
- 验收一(功能):`nplc1 tests/boot_hello.npl > boot.ssa && qbe && link` 后运行正确。
- 验收二(不动点):`nplc1 compiler.npl > il2`;用 il2 构建 `nplc2`;
  `nplc2 compiler.npl > il3`;断言 **il2 == il3**(自举到达不动点,
  cproc 式 validate;逐字节 stage0==stage1 翻译不在本阶段承诺)。

## 2. 语言面 L1(compiler.npl 允许使用 = 其 codegen 允许编译,二者同步收敛)

- 表:special forms 仅 define(function shorthand & constant var)/lambda/
  let/if;body 多表达式;内部 define 不用(规避捕获限制);
  quote:仅 symbol/整数/布尔。
- builtin:+ - * < =(dispatch table 再加即可,但源码克制使用)
  car cdr cons null? pair? eq? not display newline
  string-length string-append string-ref number->string string=? symbol->string
  新增 I/O 与控制:set-car! set-cdr!(可变计数器单元)/
  read-file(program source)/ program-arguments / exit。
- 高阶函数与闭包:**编译器源码刻意不用**(闭包捕获属高危面;
  全部用 top-level 函数 + 显式循环消除)。

## 3. 关键实现点

- argv:QBE `_main(w0=argc, x1=argv)` ABI;gen_function_to 对 main 发射
  两参签名并立刻 `call $npl_store_args`;`(program-arguments)` 返回字符串列表。
- read-file:整文件读入 box string;失败 exit(1)。
- exit:`(exit code)` → `$npl_exit`(untag, &255, _Exit)。
- emitter:`out` 为反向 string 累积流;tmp/label 计数器用 cons cell +
  set-car! 可变单元;env 为关联表(局部名 → QBE %temp 名符串)。
- 生成的 IL 与 stage0 不比对(命名私有);只保行为。


---

## 增量 4 状态 ledger(2026-08-27 checkpoint)

已完成(nplc1 可构建可运行):
- parser 输出形态经 TREE 探针验证正确:`(211 ((100 . main)) (5))`
- 三层 peek 修正;tag 211 全链统一;application 判别走 pair-head;
  runtime 全局 cell + `$npl_global_inits` 双端(main 与空体兜底)接通

遗留清单(按依赖序):
1. **词法数值域清理**(根因:string-ref 返回 tagged 字码,而字面码
   47/58 等为 raw;digit 循环 `(* acc 10)` 跨域)。方案=三个 C 钩子:
   - `npl_lex_class_w(cT, kind) → :w 0/1` —— 字符分类(is digit/ws/
     symchar/';'/'('/')'/quote),签名仿 string=?
   - `npl_lex_scan_sym_end(sT, jT) → tagged j`(连续 symchar 扫描)
   - `npl_lex_number(sT, iT, jT, isNegL) → tagged fixnum`(含符号,
     内部 strtol,全 raw 域)
2. emit-function 补 `@start` 行(当前产物缺该行,QBE 无法消费);
3. IL 拼接完成度断言:每条 func-line 必须含非空操作数(fail-fast
   自检,防"copy 空/call 空"再次静默出现);
4. tools/check_bootstrap.sh:
   a. 功能验收 — nplc1 boot_hello.npl > x.ssa;qbe+clang 链接运行;
   b. 不动点验收 — il2=nplc1 compiler.npl;nplc2 由 il2 构建;
      il3=nplc2 compiler.npl;diff il2 il3 必须 0 字节差。

## 增量 4 进度更新(2026-08-27 第二轮)

新达成:
- emit-top-level 解包错位修复(params 曾取到名字字符串 → $___ )
- sanitize 条件链逻辑修复(lower-case 分支永假),int->string 接入 route
- FUNCS 表存裸名,消除 call $$f 双 $
- 词法 class 9(#)/10,`#t/#f` 双字符 token;parse-expr 两臂 if 兼容
- parse-let 补关键字消费;data 行统一 "data " 前缀(data-line 收口)
- escape-free 字符串拼接((quoted s) 组合子,消除 \" 转义需求)
- nplc1 端 R7RS variadic string-append 折叠;runtime-init 全局
  ($npl_global_inits + data 占位槽)双端对称实现
- tools/check_bootstrap.sh:**Stage A 全绿**
  (算术=5 / 递归=7 / 带参与序 add(20,22)=42 / 字符串+begin=9 'ok')

剩余单点(Stage B 尾):
- nplc2(il2 构建的二进制)自举时崩溃于 rev_into 对 nil 的 cdr
  (v=10;nplc1 同输入正常 → c1il 产物存在一处参数/序列生成缺陷,
   仅出现在带 body 计算的被调函数;no_pie 在 arm64 不可用,
   使用 runtime anchor 差值法定位到 rev_into+0x44)
- 下一步:diff c1il.ssa 与 nplc1 自身产物中 $rev/$eval_args_fixed/
  user_call 三函数;嫌疑集中于 eval-args-fixed 累积方向或 env 链

## Stage B 缺陷精确定位(2026-08-27 第二轮末)

方法确立:
1. runtime 注入 `void npl_tracev(unsigned long)` + QBE IL 文本级插桩
   (`%tv =l call $npl_tracev(...)` 必须置于 @start 之后!)
2. PIE 下 LR→符号映射用 runtime anchor(`&g_alloc_head`)差值校准
3. ar rcs 会追加旧 .o —— 必须先 rm 再归档

发现(对 h2.npl,经 nplc2):
- rev_into 共被调 7 次,前 6 次入参为合法链指针且互差 64B,
  第 7 次入参即 nil(10)→ tokens 链在第 7 个 cell 处断裂
- 结论:c1il 生成的运行时里存在一处内存损坏(疑似某 storel
  写错槽或 inits 执行次序),使 lexer 构建的 token 链短命
- next step(cold-session ready):
  a. c1il.ssa 中给 $cell/$npl_cons 入口同法插桩,找写路径
  b. 重点复查 emit_global_slot 的 CURBUF/I-F 分支与
     declare-string-box 中遗留 last-glob-slot("$FIX_ME")死代码
  c. h2 最小化失败样例可作回归夹具

## Stage B 诊断进展 II(2026-08-27 第三轮,事件级 trace)

方法论定型(全部工具已入库):
1. `tools` 侧:IL 级全量 wrapper 包装生成器(对 npl_cdr/car/cons/
   set_car_/string_append 的 455 个 call site 自动包 trace stub,
   site*16+i 编号,tag>=8000 表返回值)
2. runtime 侧:npl_trace2(tag,val)+backtrace dump+PIE anchor 校准
3. normdiff.py/ctrldiff.py/calldiff.py 三件套归一化 IL diff
   (temp/label/data 常量名规范后对比)

关键实证(h2.npl 经 nplc2 执行,w2 event log):
- 词法流完全正常推进:tokens 链依次构建(kind 字面量 16/24/20 等
  与 tagged 编码自洽),"ok" string box、EOF guard 全部按序发生
- 崩溃点精确锁定:**rev_into 遍历 acc(tokens)至第 8 层,
  该 cell 的 cdr = 10(#f)**,而非任何合法 cell/nil
- 另一疑点(push_number 读到的数值字节与预期不符)指向
  入口数据流的 argv/read-file 链路,需下轮甄别
- stage0 同源产物对相同输入全程无恙;两次运行的差异只能来自
  执行体本身(排除静态 IL 语义差:calldiff/ctrldiff 均零差异)
- 因此问题类别判定为:**执行期内存破坏或全局初始化时序**,
  高度怀疑与 CURBUF/HAS-INIT/INIT-BUF 全局 cell 的
  $npl_global_inits 单次执行契约有关(见 emit_global_slot 分支)

下轮攻坚计划(冷启动可直入):
1. 复核 c1il.ssa 的 $npl_global_inits:storel 数量、$gv_ 占位符覆盖度;
   用 IL 文本插桩给每个 storel 前 trace(tag=GVIDX,val)
2. 审计 variable_def 与 register-func 两处 FUNCS/GLOBS 的写路径是否
   在 inits 之外仍有 set-car! 泄漏(CURBUF="I" 期间 func-line 写入
   INIT-BUF 是否污染 tokens 相关结构)
3. declare-string-box 内遗留的 last-glob-slot/"$FIX_ME"/fresh-string-
   op-declare 死代码清理(str-atom 全局初始化器从未启用)
4. 目标验收不变:check_bootstrap.sh Stage B — il2==il3 字节一致

## Stage B 诊断 III(2026-08-27 第四轮,最终定位层)

新增确证(trace w4,含 readfile/class 全事件流):
- read-file 正确读到输入文件;'('/symchar/quote/str 各类字符分类
  全部按 class 码命中预期分支;tokens kind 字面量(tagged 16/24/20)
  与定义一致;字符串字面量 box 构造正常
- 反例排除:b_min/h1 无恙,凡多 token/多参定义即触发 —
  崩溃条件与「签名参数列表/多实参构造」强相关
- fresh-cons 全部发生在 lexer/parser 正常路径上,未见越界写入;
  #f(10)/#t(6) 作为链元素出现于 rev_into 遍历中途
- 由于执行体(stage0-built nplc1 vs self-hosted nplc2)静态 IL 已证
  等价,剩余自由度仅剩:全局 cell 初值/pair 内存复用冲突,或
  c1il 对某处 raw-vs-tagged 比较的微妙差异(normdiff 归一化掩盖)

遗留疑点聚焦(下轮首选验证):
A. $binding_pairs/$collect_chain 在 arg-list 多元素时的 token 消耗
B. FUNCS/GLOBS/SYMS alists 与 CURBUF 单字节状态机交错
C. delete declare-string-box 死代码(last-glob-slot "$FIX_ME")
   并审计 pre-tagged-string-slot 是否被意外路径触达
D. 复验手段:w2 wrapper 增加 set_car_/cell_get 包装,
   对 FUNCS/GLOBS 写入打 tag 流

## Stage B 诊断 IV(同轮续,差分对照完成)

新增对照实验:对 stage0 产物(n1w)与自举产物(c1w2/w4)以相同
wrapper 机制处理 h2_clean:
- 输入侧完全一致:char-class 扫描次数均为 108(排除输入/文件差异)
- wrapper 覆盖率不等价(1007 vs 141 事件):因两版 IL 的常数传递
  方式不同(stage0 多走 %t 寄存器,全部命中 wrapper;nplc1 将部分
  raw 常量直接内联进 call,被 skip 规则漏掉)— 该 diff 维度无效,
  已记录避免误导
- 失败样例分型确认:
    b_min/h1(零参 builtin)          -> PASS
    display(str)/begin              -> FAIL(v=#f 遍历)
    用户函数 (f)/(f x)/(f a b)      -> FAIL(#t/野指针/segv)
  ⇒ 触发条件与「FUNCS 注册表的查找与遍历」以及「params/body 链」强相关,
  而非词法层

下轮首选实验(顺序执行):
1. 在 $pass_register_functions/$register_func/$lookup_func 三点插桩
   (wrapper 化 cell_get/car/cdr 已可用),dump FUNCS alist 内容
2. 对 g0 `(define (f) 9)(define (main) (f))`:若 FUNCS 注册时
   sig 解包错位((cdr (car ...)) 层级),lk-s 的 string=? 将把
   bool 当字符串比较,require_bytes 失败会在更早退出 — 若未早退
   则验证 lk-s 的 entries 非 pair 进入循环
3. 一旦定位,回归 Stage B(il2==il3)
