# Prompt

> 我希望基于 Racket / Scheme 来设计一套基于 GPU 加速的 GUI 框架，类似 GPUI，而同时抽象模型或者操作上类似 Gradio、Streamit 或者 ImGUI，或者更加现代的 Kotlin Compose、Swift UI、以及其他类似的 declarative UI。
> 
> 你来帮我对比一下这几种 API 设计思路，结合 Scheme 语言灵活的语法和表达形式，以及 call/cc 和 宏 等强大的功能，选择一个合适的方向来考虑如何设计这个 UI 框架 和 DSL。

# Response

Kimmy，我先给一个明确结论：

如果你的目标是：

- 底层有 **GPU 加速渲染能力**，接近 GPUI 这种现代本地 UI；
- 上层 API 又希望有 **现代声明式体验**，类似 SwiftUI / Kotlin Compose；
- 同时还想利用 Scheme/Racket 的 **宏、一等过程、call/cc / delimited continuations**；
- 并且不排除以后提供类似 Gradio / Streamlit 的高层快速开发模式；

那么我建议你选择一种 **混合架构**：

> **声明式组件模型为主轴：SwiftUI / Compose 风格；  
> 状态管理采用细粒度响应式信号；  
> 渲染层采用 retained scene graph + GPU display list；  
> 局部保留 immediate-mode 逃生口；  
> call/cc / delimited continuation 只用于异步流程、对话框、向导等控制抽象，而不作为每帧渲染主模型。**

一句话概括：

> **上层像 Compose / SwiftUI，底层像 GPUI / GPU immediate renderer，中间用 Scheme 宏做 DSL，用 delimited continuation 做流程控制。**

下面我展开对比，并给出一个比较合适的框架设计方向。

---

# 1. 几种 UI API 设计思路对比

我们可以把你提到的几种风格分成几类：

1. **Gradio / Streamlit 风格**
2. **ImGui 风格**
3. **SwiftUI / Kotlin Compose 风格**
4. **FRP / Elm / Signal 风格**
5. **基于 call/cc 的 continuation UI**

它们不是完全互斥的，但如果作为主 API 模型，差异非常大。

---

## 1.1 Gradio / Streamlit 风格

### 核心模型

Gradio / Streamlit 的典型思路是：

> 用户写一个类似脚本或函数的东西，框架根据输入重新执行这个脚本，然后生成 UI。

例如 Streamlit 概念上类似：

```python
name = text_input("Name")
age = slider("Age", 0, 100)

if button("Submit"):
    show_result(name, age)
```

每次交互后，整个脚本可能重新运行。框架通过 widget id、session state 来恢复状态。

Gradio 则更偏：

```python
input -> model/function -> output
```

适合做 demo、表单、ML 模型展示。

---

### 优点

- 极其适合快速原型；
- 用户心智成本低；
- 非常适合数据科学、ML、内部工具；
- 容易从函数签名自动生成 UI；
- 很适合 Scheme 做成高层表单 DSL。

例如你可以设计成：

```scheme
(define-form (demo)
  (input name "Name" text)
  (input age "Age" slider #:min 0 #:max 100)
  (output result (predict name age)))
```

这在 Racket 里可以做得很漂亮。

---

### 缺点

如果作为通用 GUI 框架主模型，问题很明显：

- 重执行粒度太粗；
- 难以表达复杂交互；
- 难以做高性能实时渲染；
- 难以精细控制动画、焦点、快捷键、拖拽、画布；
- 状态恢复依赖隐式 id，容易不稳定；
- 对 GPU 场景不够友好，除非底下还有一层更强的渲染系统。

---

### 对 Scheme 的契合度

中等偏高。

Scheme 宏很适合做这种表单 DSL：

```scheme
(form
  (field name text)
  (field age slider)
  (submit predict))
```

但它的表达上限不高。它可以作为一个 **高层应用层**，而不是整个框架的底层核心。

---

### 对 GPU 加速的契合度

一般。

Gradio / Streamlit 本身不是为实时 GPU UI 设计的。当然你可以把它们渲染到 GPU 后端，但如果交互频繁、动画复杂、画布很多，模型会受限。

---

## 1.2 ImGui 风格

### 核心模型

ImGui 是 immediate mode UI：

> 每一帧都重新调用 UI 函数，UI 函数直接产生绘制命令，框架不长期保留完整组件树。

概念上类似：

```cpp
ImGui::Begin("Window");
ImGui::Text("Hello");
if (ImGui::Button("Save")) {
    save();
}
ImGui::End();
```

每帧都会执行这些代码。

---

### 优点

- 非常简单直接；
- 和游戏循环、GPU 渲染天然契合；
- 适合工具、调试器、编辑器、节点编辑器、3D 软件；
- 状态少，心智模型直接；
- 很容易做 custom drawing；
- 对实时应用非常友好。

---

### 缺点

如果作为通用应用 UI：

- 状态管理比较原始；
- 可访问性、焦点、输入法、复杂文本编辑较难做；
- 动态列表、动画、主题、布局系统会比较复杂；
- 组件身份和状态保存需要手动 ID 管理；
- 复杂应用容易变成大型每帧副作用代码；
- 不够“声明式”，难以像 SwiftUI / Compose 那样优雅组合。

---

### 对 Scheme 的契合度

不错。

Scheme 的一等过程、闭包、参数化动态上下文很适合 immediate mode：

```scheme
(frame
  (when (button "Save")
    (save!))
  (slider "Volume" volume
          #:on-change (lambda (v) (set-volume! v))))
```

但这并不能充分发挥 Scheme 宏在声明式抽象上的优势。

---

### 对 GPU 加速的契合度

非常高。

ImGui 风格本身就是每帧生成 draw list，这和 GPU 渲染非常匹配。

但问题是：

> 它很适合作为底层渲染模型，不太适合作为唯一的上层编程模型。

---

## 1.3 SwiftUI / Kotlin Compose 风格

### 核心模型

SwiftUI / Compose 的现代声明式模型大致是：

> UI 是状态的函数。  
> 状态变化时，框架重新计算受影响的部分，并把变化同步到渲染树。

概念上：

```kotlin
@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }

    Column {
        Text("Count: $count")
        Button(onClick = { count++ }) {
            Text("+")
        }
    }
}
```

SwiftUI 类似：

```swift
struct Counter: View {
    @State var count = 0

    var body: some View {
        VStack {
            Text("Count: \(count)")
            Button("+") { count += 1 }
        }
    }
}
```

---

### 优点

这是目前最适合通用现代 GUI 的模型之一：

- 声明式，易组合；
- 组件可复用；
- 状态和视图关系清晰；
- 支持局部状态、派生状态、环境值；
- 容易做动画、主题、布局、可访问性；
- 可以增量更新，不必整棵树重建；
- 适合中大型应用；
- 非常适合做设计系统、组件库、应用框架。

---

### 缺点

实现复杂：

- 组件身份；
- 状态保存；
- 动态列表 diff；
- key；
- 重组 / reconciliation；
- 失效传播；
- 性能优化；
- 动画事务；
- 事件与副作用边界。

这些都不简单。

但从用户角度看，这个模型非常值得。

---

### 对 Scheme 的契合度

非常高。

Scheme / Racket 的几个特性非常适合：

- 宏可以定义漂亮的 UI DSL；
- 一等过程适合组件和事件处理器；
- 词法闭包适合捕获局部状态；
- `syntax-parse` 可以做编译期检查；
- 参数、动态上下文适合 theme、environment；
- 可以自定义 `#lang`；
- 可以用 delimited continuation 做流程控制；
- 可以逐步从安全抽象过渡到 unsafe 高性能层。

例如：

```scheme
(define/component (counter)
  (define count (use-state 0))

  (vstack
    (text (format "Count: ~a" (state-ref count)))
    (button "+"
      #:on-click (lambda ()
                   (state-update! count add1)))))
```

这已经很有 Compose / SwiftUI 的感觉了。

---

### 对 GPU 加速的契合度

也很好，只要你底下设计得当。

关键是：

> 上层是声明式、可保留、可失效的组件树；  
> 底层仍然可以每帧生成 GPU draw commands。

也就是说：

```text
Declarative Component Tree
        ↓
Retained Render / Layout Tree
        ↓
Dirty Invalidation
        ↓
GPU Display List / Draw Commands
```

这比纯 immediate mode 更适合复杂应用，同时仍然可以非常高性能。

---

## 1.4 FRP / Elm / Signal 风格

### 核心模型

Elm / FRP 类模型强调：

> 事件流、信号、状态折叠、单向数据流。

例如 Elm：

```elm
type Msg = Increment | Decrement

update msg model =
  case msg of
    Increment -> model + 1
    Decrement -> model - 1

view model =
  ...
```

或者基于 signal：

```scheme
(define count (signal 0))
(define label (signal-map count (lambda (n) (format "Count: ~a" n))))
```

---

### 优点

- 状态变化清晰；
- 易于推理；
- 适合数据流驱动界面；
- 和 GPU 动画、时间信号结合自然；
- 适合做响应式图表、可视化、仪表盘。

---

### 缺点

如果所有东西都强制走完整 FRP / Elm：

- 局部组件状态会变复杂；
- 事件路由容易样板化；
- 大型应用的状态更新函数容易膨胀；
- 普通开发者可能觉得绕。

---

### 推荐用法

不要把整个框架只设计成纯 Elm。

但是可以把 **signal / reactive dependency** 作为底层机制。

也就是说：

> 用户 API 像 Compose / SwiftUI；  
> 底层状态传播像 signal / fine-grained reactivity。

这是很理想的组合。

---

## 1.5 call/cc / continuation 风格

这是 Scheme 特色路线。

### 可能思路

你可以把 UI 交互写成同步式流程：

```scheme
(define (delete-file-flow file)
  (when (confirm (format "Delete ~a?" file))
    (delete-file file)
    (toast "Deleted")))
```

这里 `confirm` 表面上像同步函数，但实际上它可能：

1. 弹出一个对话框；
2. 暂停当前流程；
3. 等用户点击；
4. 恢复原来的流程。

这可以用 continuations 实现。

---

### 优点

非常适合：

- 对话框；
- 向导；
- 多步表单；
- 异步任务；
- 教学演示；
- 脚本式交互；
- 类似 “await” 的体验。

例如：

```scheme
(define/flow (new-project-flow)
  (define name (await (ask-name)))
  (define dir (await (ask-directory)))
  (when (await (confirm "Create project?"))
    (create-project name dir)))
```

这个体验非常优雅。

---

### 缺点

不适合做核心渲染模型。

原因：

- 每帧捕获 / 恢复 continuation 代价可能很高；
- GPU 资源、窗口上下文、事件循环与 continuation 生命周期不容易对齐；
- 长时间保存 continuation 容易造成资源泄漏；
- 多次恢复可能引入难以调试的状态；
- 序列化、热重载、调试都会变复杂；
- 完整 `call/cc` 太强，容易把整个运行时上下文都捕获进去。

---

### 推荐用法

不要每帧用 `call/cc` 驱动 UI。

更推荐：

> 用 **delimited continuations**，也就是带 prompt 的可组合续体，来实现局部流程暂停和恢复。

Racket 里可以考虑：

- `call-with-continuation-prompt`
- `abort-current-continuation`
- continuation marks
- parameterization
- dynamic-wind 谨慎使用

把 `await`、`confirm`、`wizard`、`modal` 做成流程控制原语。

---

# 2. 综合判断：应该选哪个主方向？

如果目标是通用、现代、GPU 加速的 GUI 框架，我建议主方向是：

> **Compose / SwiftUI 风格的声明式组件模型。**

但不是简单照抄，而是结合 Scheme 特点：

```text
Racket 宏 DSL
+
细粒度 signal / state
+
retained component tree
+
GPU render tree / display list
+
immediate-mode canvas 逃生口
+
delimited continuation 流程控制
```

---

## 为什么不是纯 Gradio / Streamlit？

因为它们更适合：

- 快速原型；
- 表单；
- ML demo；
- 服务端轻交互。

但如果你要做真正复杂的桌面 / GPU 应用，它们的上限太低。

不过你可以把它们做成一层：

```scheme
#lang gpu-ui/form
```

然后编译到底层声明式组件。

---

## 为什么不是纯 ImGui？

ImGui 很适合：

- 工具；
- 调试界面；
- 游戏编辑器；
- 实时可视化。

但作为长期应用框架，它缺少：

- 更强的组件抽象；
- 更稳定的状态模型；
- 更好的可访问性；
- 更容易维护的布局系统；
- 更现代的数据绑定。

所以 ImGui 更适合作为底层渲染或局部逃生口，而不是唯一 API。

---

## 为什么不是纯 call/cc？

call/cc 是 Scheme 的大杀器，但它更适合控制流抽象，而不是每帧渲染。

推荐把它用于：

```scheme
(await dialog)
(await user-input)
(with-modal ...)
(wizard ...)
```

而不是：

```scheme
每帧 call/cc 渲染整棵树
```

后者实现复杂、性能难控、调试困难。

---

# 3. 推荐总体架构

我建议分成几层。

---

## 3.1 架构总览

```text
┌──────────────────────────────────────────────┐
│ High-level DSL / Forms / Gradio-like layer   │
├──────────────────────────────────────────────┤
│ Component DSL                                │
│ define/component, use-state, use-signal      │
├──────────────────────────────────────────────┤
│ Reactive Core                                │
│ signals, derived values, invalidation        │
├──────────────────────────────────────────────┤
│ Retained UI Tree / Slot Table                │
│ component identity, state slots, focus       │
├──────────────────────────────────────────────┤
│ Layout System                                │
│ flexbox / grid / constraints / text layout   │
├──────────────────────────────────────────────┤
│ GPU Renderer                                 │
│ display list, batching, shaders, glyph atlas │
├──────────────────────────────────────────────┤
│ Platform Layer                               │
│ window, input, clipboard, IME, accessibility │
└──────────────────────────────────────────────┘
```

---

## 3.2 关键设计原则

### 原则一：视图是状态的函数

```scheme
(view state) -> ui-tree
```

用户不手动修改树，而是修改状态。

---

### 原则二：状态变化触发细粒度失效

不要每次状态变化都重建整个 UI。

应该建立依赖图：

```text
signal A -> text node
signal B -> progress bar
signal C -> shader uniform
```

只有依赖项变脏。

---

### 原则三：保留组件身份和状态槽

组件不是普通函数调用一次就结束。

例如：

```scheme
(define/component (counter)
  (define count (use-state 0))
  ...)
```

这里的 `count` 必须跨渲染保留。

你需要一个 slot table：

```text
component instance + lexical position -> state value
```

类似 Compose 的 slot table / React hooks 的底层思想。

---

### 原则四：渲染层仍然可以是 immediate 的

声明式不等于渲染器必须保留所有绘制对象。

你可以这样：

```text
retained semantic tree
        ↓
每帧遍历脏节点
        ↓
生成 GPU commands
```

这很接近现代高性能 UI 的做法。

---

### 原则五：给高性能场景留 immediate-mode 出口

例如：

```scheme
(canvas
  (lambda (ctx)
    (draw-chart ctx data)))
```

或者：

```scheme
(gpu-view
  #:fragment "particle.frag"
  #:uniforms (hash 'time time-signal))
```

这样不会限制框架能力。

---

# 4. 推荐 API 风格：一个 Scheme 化 Compose / SwiftUI DSL

我建议你的主 DSL 长这样。

---

## 4.1 简单组件示例

```scheme
(define/component (counter)
  (define count (use-state 0))

  (vstack #:spacing 8
    (text (format "Count: ~a" (state-ref count)))

    (hstack #:spacing 4
      (button "−"
        #:on-click (lambda ()
                     (state-update! count sub1)))

      (button "+"
        #:on-click (lambda ()
                     (state-update! count add1))))))
```

这里：

- `define/component` 定义组件；
- `use-state` 创建跨渲染保留的局部状态；
- `state-ref` 读取状态；
- `state-update!` 更新状态并触发失效；
- `vstack`、`hstack`、`text`、`button` 是布局与控件。

---

## 4.2 使用 signal 的更响应式版本

你也可以让状态直接表现为 signal：

```scheme
(define/component (counter)
  (define count (use-signal 0))

  (vstack
    (text
      (signal-map count
        (lambda (n)
          (format "Count: ~a" n))))

    (button "+"
      #:on-click (lambda ()
                   (signal-update! count add1)))))
```

这个模型非常适合细粒度更新。

例如 `text` 可以接受一个 signal：

```scheme
(text some-signal)
```

当 `some-signal` 变化时，只有这个文本节点变脏。

---

## 4.3 双向绑定

输入框常见需求是双向绑定。

可以设计成：

```scheme
(define/component (name-input)
  (define name (use-signal ""))

  (vstack
    (text-input
      #:value name
      #:placeholder "Your name")

    (text
      (signal-map name
        (lambda (s)
          (format "Hello, ~a" s))))))
```

`#:value name` 是一个 binding：

- 读取时显示 `name` 当前值；
- 用户输入时写回 `name`；
- 其他地方依赖 `name` 会自动更新。

这比 Gradio 的函数式输入输出更灵活，也比裸 ImGui 更易维护。

---

## 4.4 动态列表和 key

动态列表一定要考虑 key：

```scheme
(define/component (todo-list)
  (define items (use-signal '()))

  (vstack
    (for/list ([item (signal-ref items)]
               #:key (todo-id item))
      (todo-row item))))
```

这里：

```scheme
#:key (todo-id item)
```

非常重要。

没有稳定 key，框架无法正确保留列表项状态、动画和焦点。

---

## 4.5 环境值和主题

类似 SwiftUI 的 environment / Compose 的 Local：

```scheme
(with-theme dark-theme
  (app-root))
```

或者：

```scheme
(provide-context current-user user)
(use-context current-user)
```

在 Racket 中可以用参数、continuation marks，或者自己的 reactive context 实现。

如果要响应式，最好不要只用普通 parameter，因为 parameter 变化不会自动触发 UI 更新。可以包一层：

```scheme
(define-context current-theme (light-theme))
```

内部其实是 signal 或依赖追踪对象。

---

# 5. 宏在其中的作用

Scheme 宏非常适合做这个框架，但你要注意：**不要把所有东西都做成宏。**

---

## 5.1 什么适合做成宏？

适合做成宏的：

- `define/component`
- `use-state`
- `use-signal`
- `use-memo`
- `on-click`
- `with-theme`
- `await`
- `form`
- `match-view`
- `for/list` 的 UI 变体
- 编译期属性检查
- 组件 ID / slot ID 生成
- 语法糖和错误提示

---

## 5.2 什么适合做成普通函数或结构体？

适合做成普通运行时代象的：

- 节点构造函数；
- 样式对象；
- 颜色、字体、边距；
- 事件结构；
- GPU 资源句柄；
- 布局树；
- 渲染命令。

宏负责语法抽象，运行时对象负责数据和性能。

---

## 5.3 一个简化的宏方向

例如：

```scheme
(define-syntax (define/component stx)
  (syntax-parse stx
    [(_ (name args ...) body ...)
     #'(define (name args ...)
         (with-component-context 'name
           body ...))]))
```

实际上你会做更多：

- 收集 `use-*` 调用；
- 生成稳定槽位；
- 检查是否在条件分支中错误使用；
- 附加源码位置；
- 支持热重载；
- 生成调试名；
- 生成组件元数据。

Racket 的 `syntax-parse` 非常适合做这些。

---

# 6. call/cc / delimited continuation 的正确位置

这是 Scheme 最有特色的地方，值得单独讲。

---

## 6.1 不要把 continuation 作为主渲染模型

不建议：

```text
每帧渲染 = 捕获/恢复 continuation
```

原因：

- 性能难控；
- 资源生命周期复杂；
- GPU 上下文难以安全恢复；
- 调试困难；
- 热重载困难；
- 容易意外保留大量对象。

---

## 6.2 推荐用 delimited continuation 做流程控制

适合：

```scheme
(await ...)
(confirm ...)
(wizard ...)
(with-modal ...)
(run-flow ...)
```

例如：

```scheme
(define/flow (delete-item-flow item)
  (when (await (confirm-dialog
                 (format "Delete ~a?" (item-name item))))
    (delete-item! item)
    (await (toast "Deleted"))))
```

这里：

1. `confirm-dialog` 不是同步阻塞；
2. 它会暂停当前流程；
3. 渲染对话框；
4. 用户点击后恢复流程；
5. `await` 返回用户选择。

这非常优雅，而且不影响主渲染循环。

---

## 6.3 用 prompt 限制续体范围

不要随便使用完整 `call/cc`。

建议使用 delimited continuation：

```scheme
(call-with-continuation-prompt
 (lambda ()
   (flow-body))
 ui-flow-prompt-tag
 (lambda (k)
   (suspend-flow k)))
```

然后 `await` 内部可以：

```scheme
(abort-current-continuation
 ui-flow-prompt-tag
 (lambda (k)
   (register-pending-flow k)))
```

这样续体只捕获你允许的那一段流程，而不是整个程序。

---

## 6.4 continuation 适合做 “同步式异步”

比如：

```scheme
(define/flow (import-project-flow)
  (define file (await (pick-file)))
  (define options (await (import-options-dialog file)))
  (define result (await (run-import-task options)))
  (toast (format "Imported ~a" result)))
```

这种代码比回调地狱舒服太多。

这正是 Scheme 的优势。

---

# 7. 与 GPUI 的关系

你提到类似 GPUI。

GPUI 类框架通常有几个特点：

- 高性能渲染；
- GPU 加速；
- 本地窗口和输入；
- 布局系统；
- 文本渲染；
- 样式系统；
- entity / view / element 分层；
- 事件和动作系统。

你可以借鉴：

```text
Entity / Component State
        ↓
Render Function
        ↓
Element Tree
        ↓
Layout
        ↓
Paint / Display List
        ↓
GPU
```

但用 Scheme 宏把 API 做得更声明式。

例如：

```scheme
(define/component (editor)
  (define doc (use-model empty-document))

  (window
    (toolbar
      (button "Save" #:action save-action))
    (canvas
      (lambda (ctx)
        (draw-document ctx (model-ref doc))))))
```

底层可以是 GPUI 风格的 retained / display-list 渲染，上层是 Scheme DSL。

---

# 8. 一个更完整的 DSL 示例

下面是一个假想但合理的目标 API。

---

## 8.1 应用入口

```scheme
(define/app (main)
  (window
    #:title "Demo"
    #:size (size 900 600)
    (theme-provider dark-theme
      (root-view))))
```

---

## 8.2 状态与派生状态

```scheme
(define/component (profile-card)
  (define name (use-signal "Kimmy"))
  (define age (use-signal 20))

  (define description
    (use-derived
      (list name age)
      (lambda (n a)
        (format "~a, age ~a" n a))))

  (vstack #:spacing 6
    (text description #:style 'title)
    (hstack
      (button "Birthday"
        #:on-click (lambda ()
                     (signal-update! age add1))))))
```

这里：

- `use-signal` 创建可变响应式状态；
- `use-derived` 创建派生值；
- 当 `age` 变化，`description` 自动更新；
- 只有依赖这些信号的节点会失效。

---

## 8.3 表单和双向绑定

```scheme
(define/component (signup-form)
  (define email (use-signal ""))
  (define password (use-signal ""))
  (define remember? (use-signal #f))

  (form-card
    (text-input
      #:label "Email"
      #:value email)

    (password-input
      #:label "Password"
      #:value password)

    (checkbox
      #:label "Remember me"
      #:value remember?)

    (button "Submit"
      #:enabled (signal-map email non-empty?)
      #:on-click (lambda ()
                   (submit! (signal-ref email)
                            (signal-ref password)
                            (signal-ref remember?))))))
```

这个体验接近现代声明式框架，但比 React 更轻，比 Gradio 更可控。

---

## 8.4 列表和状态保留

```scheme
(define/component (todo-app)
  (define todos (use-signal '()))
  (define draft (use-signal ""))

  (vstack
    (hstack
      (text-input
        #:value draft
        #:placeholder "What needs to be done?")

      (button "Add"
        #:on-click
        (lambda ()
          (unless (string=? (signal-ref draft) "")
            (signal-update! todos
              (lambda (xs)
                (cons (make-todo (signal-ref draft)) xs)))
            (signal-set! draft "")))))

    (scroll-view
      (vstack
        (for/list ([todo (signal-ref todos)]
                   #:key (todo-id todo))
          (todo-row todo todos))))))
```

---

## 8.5 GPU 自定义绘制

```scheme
(define/component (audio-visualizer)
  (define spectrum (use-signal #f32-vector))

  (canvas
    #:on-draw
    (lambda (ctx)
      (draw-bars ctx (signal-ref spectrum)
                 #:color '(0.2 0.8 1.0 1.0)
                 #:radius 4))))
```

更底层一点：

```scheme
(gpu-view
  #:vertex "shader.vert"
  #:fragment "shader.frag"
  #:uniforms
  (hash
    'time time-signal
    'resolution resolution-signal
    'mouse mouse-signal))
```

这让你能做真正的 GPU 内容，而不是只是普通控件。

---

## 8.6 流程控制

```scheme
(define/component (danger-zone)
  (button "Delete Project"
    #:style 'danger
    #:on-click
    (lambda ()
      (spawn-flow
        (when (await (confirm-dialog "Delete this project?"))
          (delete-project!)
          (await (toast "Project deleted")))))))
```

这里 `spawn-flow` 启动一个 continuation-based flow。

---

# 9. 核心语义设计建议

这部分很重要。

---

## 9.1 组件不是普通函数

组件函数会被多次执行，但状态必须保留。

因此：

```scheme
(define/component (counter)
  (define count (use-state 0))
  ...)
```

不能简单等价于：

```scheme
(define (counter)
  (let ([count 0])
    ...))
```

`use-state` 必须从组件实例对应的状态槽中读取旧值。

---

## 9.2 组件身份必须稳定

组件身份来源：

- 词法位置；
- 显式 `#:id`；
- 列表中的 `#:key`；
- 分支结构中的稳定路径。

例如：

```scheme
(if editing?
    (editor-field #:id "editor")
    (text-view #:id "editor"))
```

如果两个分支共享同一个稳定身份，状态可以保留；否则状态会重建。

---

## 9.3 事件处理器应运行在事务中

比如用户点击按钮：

```scheme
(lambda ()
  (signal-set! a 1)
  (signal-set! b 2)
  (signal-update! c add1))
```

框架应把这些变更合并成一次更新：

```text
event handler
  -> mark signals dirty
  -> batch invalidation
  -> run effects
  -> layout
  -> paint
```

避免多次布局、多次渲染、多帧不一致。

---

## 9.4 渲染线程与逻辑线程

早期建议单线程事件循环，简单可靠。

后期如果要高性能：

```text
UI logic thread
render thread
GPU present
```

但要非常小心：

- 信号读写同步；
- GPU 资源生命周期；
- 事件顺序；
- continuation 恢复线程；
- 文本布局缓存；
- 可访问性树同步。

---

# 10. 渲染架构建议

---

## 10.1 不要一开始就做完整自绘文本和布局

GPU UI 最难的不是画矩形，而是：

- 文本整形；
- 多语言；
- 输入法；
- 光标；
- 选中；
- 换行；
- 字体回退；
- 可访问性；
- 焦点；
- 快捷键；
- 剪贴板。

建议分阶段。

---

## 10.2 底层渲染对象

可以设计成：

```scheme
(rect ...)
(rounded-rect ...)
(text-run ...)
(image ...)
(clip ...)
(transform ...)
(shader ...)
(custom-draw ...)
```

这些是低层元素。

上层组件编译成这些元素。

---

## 10.3 Display List

每帧不一定直接调用 GPU。

可以先生成中间命令：

```text
PushClip
DrawRect
DrawText
DrawImage
DrawCustomShader
PopClip
```

然后 batching：

```text
按 shader / texture / clip / z-index 合并
```

这样可以减少 draw call。

---

## 10.4 文本和图标

建议一开始使用：

- glyph atlas；
- glyph cache；
- text run cache；
- shaping cache。

如果跨平台，可以考虑通过 C/Rust FFI 接入：

- HarfBuzz；
- FreeType；
- CoreText；
- DirectWrite；
- swash / fontdue / cosmic-text 等 Rust 生态；
- 或自研简化版。

Racket 通过 FFI 调 C 是可行的，但现代 GPU 生态里通过一个 Rust 中间层可能更稳。

---

# 11. 状态模型建议：细粒度 Signal 是核心

我建议你采用细粒度响应式状态。

---

## 11.1 基本原语

```scheme
(signal value)
(signal-ref s)
(signal-set! s v)
(signal-update! s f)
(signal-map s f)
(signal-combine a b f)
(use-derived deps f)
```

---

## 11.2 依赖追踪

当视图读取某个 signal：

```scheme
(signal-ref count)
```

框架记录：

```text
current-node depends on count
```

当 `count` 改变：

```text
mark dependent nodes dirty
```

---

## 11.3 组件级状态

```scheme
(use-state init)
(use-signal init)
(use-memo expr)
(use-effect deps thunk)
(use-resource acquire dispose)
```

这些都可以宏或运行时协同实现。

---

## 11.4 副作用

例如：

```scheme
(use-effect
  (list user-id)
  (lambda ()
    (load-profile user-id)
    (lambda ()
      (cancel-profile-load user-id))))
```

这类似现代框架的 effect。

---

# 12. 是否要提供类似 Gradio / Streamlit 的高层模式？

要，但作为上层，不是核心。

---

## 12.1 Form DSL

可以设计：

```scheme
(define-form (model-demo)
  (input prompt text)
  (input temperature slider #:min 0 #:max 1)
  (input model choice #:options '("small" "large"))

  (output result
    (generate prompt temperature model)))
```

它被编译为：

```scheme
(define/component (model-demo)
  (define prompt (use-signal ""))
  (define temperature (use-signal 0.7))
  (define model (use-signal "small"))

  (form-layout
    ...))
```

这样你同时拥有：

- Gradio 的易用性；
- 声明式框架的可组合性；
- GPU 渲染的高性能。

---

## 12.2 Script Mode

也可以支持脚本模式：

```scheme
(define/script (demo)
  (define name (text-input "Name"))
  (define age (slider "Age" 0 100))
  (if (button "Submit")
      (text (format "~a is ~a" name age))
      (text "Fill the form")))
```

这可以底层用：

- 状态存储；
- 稳定控件 id；
- 重新执行脚本；
- 或 continuation 暂停 / 恢复。

但这是高级糖。

---

# 13. 不同方向的适配建议

你可以根据目标用户调整。

---

## 13.1 如果主要面向应用开发

选择：

```text
Compose / SwiftUI 风格
+
信号状态
+
组件树
```

这是最佳路线。

---

## 13.2 如果主要面向游戏工具 / 编辑器

保留声明式主模型，但强化：

```scheme
(canvas ...)
(frame ...)
(immediate ...)
(draw ...)
(gpu-view ...)
```

甚至可以提供双模式：

```scheme
(define/frame ...)
(define/component ...)
```

---

## 13.3 如果主要面向数据科学 / ML

提供：

```scheme
form
input
output
plot
table
model-card
```

但底层仍然应该是声明式组件系统。

---

# 14. 推荐的框架命名概念

不一定真的用，但有助于思考。

可以叫它：

```text
Declarative Retained Reactive GPU UI
```

或者：

```text
Scheme Compositional GPU UI
```

核心概念：

- `component`：可组合视图函数；
- `signal`：响应式状态；
- `slot`：组件状态槽；
- `element`：UI 节点；
- `canvas`：自定义绘制；
- `flow`：continuation 驱动流程；
- `display-list`：GPU 渲染命令。

---

# 15. 一个推荐的 MVP 路线

不要一上来做完整 SwiftUI。

建议分阶段。

---

## Phase 1：GPU immediate renderer

先做最小渲染：

- 窗口；
- 事件循环；
- 矩形；
- 文本；
- 图片；
- 基础输入；
- immediate mode API。

目标：

```scheme
(frame
  (draw-rect ...)
  (draw-text ...))
```

先证明 GPU 后端可用。

---

## Phase 2：保留树和布局

加入：

```text
node tree
layout
style
hit test
focus
```

目标：

```scheme
(vstack
  (text "Hello")
  (button "Click"))
```

---

## Phase 3：signal 和失效机制

加入：

```scheme
signal
signal-map
dirty propagation
batch update
```

目标：

```scheme
(text (signal-map count ...))
```

---

## Phase 4：组件模型

加入：

```scheme
define/component
use-state
use-signal
use-memo
```

目标：

```scheme
(define/component (counter) ...)
```

---

## Phase 5：高层控件和主题

加入：

- text input；
- scroll view；
- list view；
- checkbox；
- slider；
- popup；
- menu；
- theme；
- animation。

---

## Phase 6：流程和开发者体验

加入：

```scheme
await
confirm-dialog
toast
wizard
hot reload
inspector
```

---

# 16. 主要风险和难点

你需要提前知道坑在哪里。

---

## 16.1 组件身份

这是声明式 UI 最难的地方之一。

问题：

- 条件分支切换后状态怎么办？
- 列表重排后输入框内容怎么办？
- 组件移动后动画如何继续？
- 没有 key 时如何降级？

建议：

- 强制动态列表提供 `#:key`；
- 提供调试工具显示组件身份；
- 对不稳定身份发出警告。

---

## 16.2 状态和效果的边界

如果视图函数里有副作用，会很难维护。

应尽量要求：

```scheme
view function = pure description
event handler / effect = side effects
```

虽然 Scheme 不强制，但可以通过约定和工具检查。

---

## 16.3 continuation 泄漏

长时间保存续体会保留大量对象。

建议：

- flow 有明确生命周期；
- 可取消；
- 可超时；
- 不要跨应用重载保存；
- 不要把 GPU 资源放在续体里。

---

## 16.4 宏过度复杂

不要一开始设计太复杂的宏。

先做可用 API：

```scheme
(use-signal)
(signal-ref)
(signal-update!)
```

再逐步加糖。

---

## 16.5 文本和输入法

如果你要做真正桌面框架，文本系统会占很多工作量。

早期可以先支持：

- 单行文本；
- 基础光标；
- 基础剪贴板；
- 基础选择。

然后再做：

- IME；
- 多段落；
- bidi；
- rich text；
- accessibility。

---

# 17. 最终推荐方案

我把建议浓缩成一个方案。

---

## 核心 API 模型

```text
SwiftUI / Compose 风格
```

---

## 状态模型

```text
细粒度 signal + component state slot
```

---

## 渲染模型

```text
retained semantic tree
+
incremental invalidation
+
GPU display list / immediate rendering
```

---

## DSL 实现

```text
Racket syntax-parse 宏
+
普通运行时结构体 / 函数
+
可选 #lang
```

---

## continuation 用途

```text
dialog / wizard / async flow / await
```

不要用于主渲染循环。

---

## immediate mode 用途

```text
canvas / debug tools / shaders / high-frequency visualization
```

作为逃生口，而不是唯一模型。

---

# 18. 一个理想中的最终形态

最终你可能得到这样的代码：

```scheme
#lang gpu-ui

(define/app (main)
  (window
    #:title "Scheme GPU UI"
    #:size (size 1000 700)
    (theme-provider dark-theme
      (split-view
        (sidebar
          (nav-item "Home")
          (nav-item "Settings"))

        (main-panel
          (counter)
          (todo-app)
          (audio-visualizer))))))

(define/component (counter)
  (define count (use-signal 0))

  (card
    (vstack #:spacing 8
      (text
        (signal-map count
          (lambda (n)
            (format "Count: ~a" n)))
        #:style 'title)

      (hstack
        (button "−"
          #:on-click (lambda ()
                       (signal-update! count sub1)))

        (button "+"
          #:on-click (lambda ()
                       (signal-update! count add1)))))))

(define/component (danger-zone)
  (button "Delete"
    #:style 'danger
    #:on-click
    (lambda ()
      (spawn-flow
        (when (await (confirm-dialog "Delete item?"))
          (delete-item!)
          (await (toast "Deleted")))))))
```

这个形态既有：

- 现代声明式；
- 细粒度状态；
- GPU 渲染；
- Scheme 宏；
- continuation 流程；
- 高层组件抽象。

---

# 19. 我的最终建议

如果你要做一个有长期潜力的框架，我建议你不要选择单一路径，而是采用如下路线：

> **主框架：Compose / SwiftUI 风格声明式组件。**  
> **状态层：细粒度响应式 signal。**  
> **渲染层：retained tree + GPU display list。**  
> **工具层：可选 immediate-mode canvas。**  
> **流程层：delimited continuation 驱动的 `await / flow / wizard`。**  
> **高快速原型层：可选的 Gradio / Streamlit 风格 form DSL。**

这条路线最能发挥 Racket/Scheme 的优势：

- 宏可以塑造优雅的 DSL；
- 闭包和一等过程适合组件和事件；
- 细粒度响应式适合性能优化；
- delimited continuation 可以提供独特而优雅的流程控制；
- 底层仍然可以保持 GPU 高性能。

如果只选一个最重要的方向，那就是：

> **做“Scheme 化的 Compose/SwiftUI”，而不是做“Scheme 化的 ImGui”或“Scheme 化的 Gradio”。**

ImGui 和 Gradio 可以作为子模式存在，但不应该是框架的唯一灵魂。
