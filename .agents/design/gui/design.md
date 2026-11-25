可以。下面我给你一个**面向“现代声明式 UI + GPU 渲染 + 游戏开发”三位一体**的完整架构蓝图。

这个架构的核心思想是：

> **UI 层采用声明式组件 + 响应式状态；  
> 游戏层采用世界 / 场景 / 实体 / 系统模型；  
> 底层共享同一套 GPU 渲染管线、draw context、shader/material 系统。**

也就是说，你不是做两个完全分裂的框架，而是做一个统一的 **GPU 应用框架**：

```text
应用
├── 声明式 UI
│   ├── 组件
│   ├── 布局
│   ├── 样式
│   ├── 信号 / 状态
│   └── 事件
├── 游戏层
│   ├── 世界状态
│   ├── 帧更新
│   ├── 相机
│   ├── 精灵 / 动画 / 瓦片
│   ├── 输入动作
│   └── 可选 ECS
└── 共享底层
    ├── 窗口 / 输入 / 平台
    ├── 资源管理
    ├── 文本 / 字体
    ├── 布局引擎
    ├── 渲染命令
    ├── GPU backend
    └── Shader / Material
```

下面我按层次展开。

---

# 1. 总体设计目标

你的框架应该同时满足以下几类需求。

---

## 1.1 现代声明式 UI

类似 SwiftUI / Compose：

```racket
(define/component (counter)
  (define count (use-signal 0))

  (vstack
    (text (signal-map count (lambda (n) (format "Count: ~a" n))))
    (button "+"
      #:on-click (lambda ()
                   (signal-update! count add1)))))
```

特点：

- 组件化；
- 状态驱动；
- 局部状态；
- 可组合；
- 可主题化；
- 可增量更新。

---

## 1.2 GPU 加速渲染

底层需要支持：

- 矩形 / 圆角矩形；
- 文本；
- 图片；
- 自定义网格；
- shader；
- render target；
- post-process；
- batching；
- instancing；
- clip / layer / blend；
- MSAA；
- high DPI。

---

## 1.3 游戏开发接口

游戏部分不能完全套普通 UI 模型，因为游戏通常有：

- 每帧更新；
- 固定时间步；
- 相机；
- 精灵；
- 动画；
- 碰撞；
- 物理；
- 输入动作；
- 大量动态实体；
- 自定义 shader；
- 高性能绘制。

所以要提供：

```racket
(game-view
  #:init create-world
  #:update update-world!
  #:draw draw-world!
  #:fixed-step 1/60)
```

这种接口。

---

## 1.4 统一而不混乱

建议明确区分三类状态：

| 状态类型 | 适用场景 | 推荐机制 |
|---|---|---|
| UI 状态 | 按钮、输入框、主题、菜单 | signal / component state |
| 游戏世界状态 | 玩家、敌人、物理、关卡 | 普通 mutable world / ECS |
| 桥接状态 | 分数、血量、暂停状态 | signal 或 event channel |

不要把整个游戏世界都塞进细粒度 signal。  
否则每帧成千上万实体变化会导致响应式系统开销过大。

推荐：

> **UI 用 signal；游戏世界用 world / ECS；只把 HUD 需要的少量数据暴露成 signal。**

---

# 2. 总体架构分层

我建议分成 10 层。

```text
┌────────────────────────────────────────────────────────────┐
│ Application DSL / #lang                                    │
│ define/app, define/component, define/theme, define/material│
├────────────────────────────────────────────────────────────┤
│ High-level UI Components                                   │
│ button, text-input, slider, list, popup, menu, dialog      │
├────────────────────────────────────────────────────────────┤
│ Game Layer                                                 │
│ game-view, world, camera, sprite, tilemap, animation, ECS  │
├────────────────────────────────────────────────────────────┤
│ Reactive State / Component State                           │
│ signal, derived, effect, slot-table, use-state             │
├────────────────────────────────────────────────────────────┤
│ Scene / Element Tree                                       │
│ UI layout tree + game scene tree + overlay tree            │
├────────────────────────────────────────────────────────────┤
│ Layout System                                              │
│ flexbox/grid/absolute, constraints, hit test               │
├────────────────────────────────────────────────────────────┤
│ Animation System                                           │
│ tween, spring, keyframe, timeline, skeleton                │
├────────────────────────────────────────────────────────────┤
│ Render Graph / Display List                                │
│ draw commands, batching, clipping, layers, passes          │
├────────────────────────────────────────────────────────────┤
│ GPU Backend / Shader / Material                            │
│ buffers, textures, pipelines, uniforms, render targets     │
├────────────────────────────────────────────────────────────┤
│ Platform Layer                                             │
│ window, input, IME, clipboard, filesystem, audio, a11y     │
└────────────────────────────────────────────────────────────┘
```

---

# 3. 模块划分

下面是一个比较完整的模块结构。

你可以把它理解成未来项目里的包结构。

```text
gpu-ui/
├── math.rkt
├── color.rkt
├── time.rkt
├── signal.rkt
├── app.rkt
├── platform.rkt
├── window.rkt
├── input.rkt
├── action-map.rkt
├── asset.rkt
├── texture.rkt
├── font.rkt
├── text.rkt
├── style.rkt
├── theme.rkt
├── layout.rkt
├── element.rkt
├── component.rkt
├── draw.rkt
├── render.rkt
├── pipeline.rkt
├── gpu/
│   ├── backend.rkt
│   ├── buffer.rkt
│   ├── texture.rkt
│   ├── sampler.rkt
│   ├── pipeline.rkt
│   ├── shader.rkt
│   ├── uniform.rkt
│   └── render-target.rkt
├── ui/
│   ├── basic.rkt
│   ├── button.rkt
│   ├── input.rkt
│   ├── scroll.rkt
│   ├── list.rkt
│   ├── popup.rkt
│   ├── menu.rkt
│   ├── dialog.rkt
│   └── focus.rkt
├── game/
│   ├── game-view.rkt
│   ├── world.rkt
│   ├── camera.rkt
│   ├── sprite.rkt
│   ├── animation.rkt
│   ├── tilemap.rkt
│   ├── input-action.rkt
│   ├── physics.rkt
│   └── ecs.rkt
├── shader/
│   ├── material.rkt
│   ├── builtin.rkt
│   └── post-process.rkt
└── devtools/
    ├── inspector.rkt
    ├── profiler.rkt
    └── hot-reload.rkt
```

---

# 4. 各模块职责

下面列出关键模块的职责。

---

## 4.1 `math.rkt`

提供基础数学类型：

```racket
vec2
vec3
vec4
mat3
mat4
rect
size
point
```

建议提供：

```racket
(struct vec2 (x y))
(struct vec3 (x y z))
(struct rect (x y w h))
(struct size (w h))
```

以及：

```racket
vec2-add
vec2-sub
vec2-scale
vec2-length
rect-contains?
rect-intersects?
mat3-translate
mat3-rotate
mat3-scale
```

---

## 4.2 `color.rkt`

颜色系统：

```racket
(struct color (r g b a))
```

提供：

```racket
rgb
rgba
hex-color
color-mix
color-alpha
theme-color
```

建议内部使用线性颜色或 sRGB 明确区分。

---

## 4.3 `signal.rkt`

响应式状态核心。

基础接口：

```racket
(signal initial-value)
(signal-ref s)
(signal-set! s v)
(signal-update! s f)
(signal-map s f)
(signal-combine s1 s2 f)
(derived deps f)
```

例如：

```racket
(define count (signal 0))
(define double (signal-map count (lambda (n) (* n 2))))
```

UI 组件读取 signal 时自动注册依赖：

```racket
(text (signal-map count (lambda (n) (number->string n))))
```

当 `count` 改变，只刷新依赖它的节点。

---

## 4.4 `component.rkt`

组件运行时。

提供：

```racket
define/component
use-state
use-signal
use-memo
use-effect
use-resource
use-context
provide-context
```

组件不是普通函数，而是有状态槽位的可重入渲染单元。

例如：

```racket
(define/component (counter)
  (define count (use-signal 0))
  ...)
```

`count` 必须跨多次渲染保留。

---

## 4.5 `element.rkt`

定义 UI 元素。

例如：

```racket
rect
text
image
button
vstack
hstack
zstack
scroll-view
canvas
gpu-view
```

每个 element 本质上可以是一个结构体：

```racket
(struct element (type props children key))
```

但对外暴露为函数或宏。

---

## 4.6 `layout.rkt`

布局系统。

需要支持：

- 固定尺寸；
- 弹性尺寸；
- 最大 / 最小尺寸；
- padding；
- margin；
- gap；
- alignment；
- flex grow / shrink；
- grid；
- absolute overlay。

核心概念：

```racket
(struct constraints (min-w max-w min-h max-h))
(struct layout-result (w h placements))
```

布局协议：

```text
measure(node, constraints) -> size
place(node, size, children) -> child placements
```

可以早期实现简单 flexbox，后期再扩展。

---

## 4.7 `style.rkt` / `theme.rkt`

样式系统。

建议将样式分为：

1. **design tokens**：颜色、字体、圆角、间距；
2. **component styles**：按钮、输入框、列表等样式；
3. **状态样式**：hover、active、disabled、focused。

例如：

```racket
(theme
  #:color-background ...
  #:color-surface ...
  #:color-primary ...
  #:font-default ...
  #:radius-md 8
  #:spacing-md 12)
```

组件样式：

```racket
(component-style button
  #:background (theme-color 'primary)
  #:foreground (theme-color 'on-primary)
  #:padding (insets 8 12)
  #:radius 6
  #:hover (style #:background (theme-color 'primary-hover))
  #:disabled (style #:opacity 0.5))
```

---

## 4.8 `draw.rkt`

基础绘制接口。

这是游戏和 UI 共享的核心。

后面我会单独详细列。

---

## 4.9 `render.rkt`

渲染命令系统。

职责：

- 收集 draw commands；
- 排序；
- 合批；
- 处理 clip；
- 处理 layer；
- 提交给 GPU；
- 管理 frame graph / render pass。

中间表示可以是：

```racket
(struct draw-cmd
  (z layer clip transform material vertex-data uniforms textures))
```

---

## 4.10 `gpu/backend.rkt`

GPU 后端抽象。

可以支持：

- OpenGL；
- Vulkan；
- Metal；
- WebGPU；
- DirectX。

从 Racket 角度，最现实的方式通常是：

> 通过 FFI 调用 C/Rust 渲染层，例如 `wgpu-native`、OpenGL loader、或自写 C 渲染库。

后端接口大致包括：

```racket
gpu-create-buffer
gpu-create-texture
gpu-create-pipeline
gpu-bind-uniforms
gpu-draw
gpu-draw-instanced
gpu-begin-pass
gpu-end-pass
gpu-present
```

---

## 4.11 `game/game-view.rkt`

游戏视图接口。

这是游戏开发的核心入口。

```racket
(game-view
  #:init create-world
  #:update update-world!
  #:draw draw-world!
  #:fixed-step 1/60
  #:camera world-camera
  #:input-map game-input-map)
```

它本质上是一个特殊组件，内部运行游戏循环。

---

## 4.12 `game/ecs.rkt`

可选 ECS。

如果做较大规模游戏，建议提供轻量 ECS：

```racket
entity
component
system
query
```

例如：

```racket
(define-component position (x y))
(define-component velocity (dx dy))
(define-component sprite (texture size tint))

(define-system movement-system
  (query position velocity)
  (lambda (e pos vel dt)
    (set-position-x! pos (+ (position-x pos) (* (velocity-dx vel) dt)))
    (set-position-y! pos (+ (position-y pos) (* (velocity-dy vel) dt)))))
```

不过第一版可以不做完整 ECS，只提供普通 `world` 结构。

---

# 5. 核心数据结构

下面是一些推荐的核心数据结构。

---

## 5.1 基础几何

```racket
(struct vec2 (x y) #:transparent)
(struct vec3 (x y z) #:transparent)
(struct rect (x y w h) #:transparent)
(struct size (w h) #:transparent)
(struct insets (top right bottom left) #:transparent)
```

---

## 5.2 颜色

```racket
(struct color (r g b a) #:transparent)
```

---

## 5.3 Signal

```racket
(struct signal
  (id
   value
   subscribers
   version)
  #:mutable)
```

实际实现里可能还需要：

- 依赖图；
- 脏标记；
- 拓扑排序；
- 批处理队列；
- 事务计数器。

---

## 5.4 元素节点

```racket
(struct ui-element
  (type
   props
   children
   key
   style
   events)
  #:transparent)
```

---

## 5.5 组件实例

```racket
(struct component-instance
  (id
   component-name
   slot-table
   state-map
   effect-list
   parent
   children)
  #:mutable)
```

---

## 5.6 布局节点

```racket
(struct layout-node
  (element
   style
   constraints
   measured-size
   placement
   children)
  #:mutable)
```

---

## 5.7 布局约束

```racket
(struct constraints
  (min-w max-w min-h max-h)
  #:transparent)
```

---

## 5.8 布局结果

```racket
(struct layout-result
  (width height placements)
  #:transparent)
```

---

## 5.9 样式

```racket
(struct style
  (width
   height
   min-width
   max-width
   min-height
   max-height
   padding
   margin
   gap
   background
   foreground
   border
   radius
   font
   font-size
   opacity
   transform
   flex-grow
   flex-shrink
   align
   justify)
  #:transparent)
```

---

## 5.10 纹理

```racket
(struct texture
  (handle
   width
   height
   format
   sampler
   source)
  #:mutable)
```

---

## 5.11 网格

```racket
(struct mesh
  (vertex-buffer
   index-buffer
   vertex-count
   index-count
   topology
   vertex-layout)
  #:mutable)
```

---

## 5.12 Material

```racket
(struct material
  (id
   pipeline
   uniforms
   textures
   blend-mode
   depth-mode
   stencil-mode
   cull-mode)
  #:transparent)
```

---

## 5.13 Shader

```racket
(struct shader-module
  (stage
   source-path
   entry-point
   compiled-handle)
  #:mutable)
```

---

## 5.14 Draw command

```racket
(struct draw-cmd
  (z
   layer
   transform
   clip
   material
   vertex-source
   uniforms
   textures)
  #:transparent)
```

---

## 5.15 Camera

```racket
(struct camera
  (projection
   view
   viewport
   z-near
   z-far)
  #:mutable)
```

对于 2D：

```racket
(struct camera-2d
  (center
   zoom
   rotation
   viewport)
  #:mutable)
```

---

## 5.16 Game World

普通游戏世界可以是任意结构体：

```racket
(struct world
  (camera
   player
   enemies
   projectiles
   tilemap
   assets
   time
   paused?
   hp-signal
   score-signal)
  #:mutable)
```

重点是：

> 世界状态不一定全部响应式。  
> 只有 UI 需要的数据才同步到 signal。

---

# 6. 主循环设计

你需要一个统一的主循环。

---

## 6.1 帧流程

推荐一帧的流程如下：

```text
1. 拉取平台事件
   ├── 键盘
   ├── 鼠标
   ├── 触摸
   ├── 手柄
   ├── 窗口大小
   ├── IME
   └── 文件拖拽

2. 更新输入状态
   └── 生成 InputState / ActionState

3. 更新游戏世界
   ├── 固定时间步 update
   ├── 可变时间步 update
   └── 动画 / 物理

4. 处理 UI 状态变更
   ├── 事件命中
   ├── 焦点
   ├── 按钮点击
   ├── 输入框编辑
   └── 信号更新

5. 执行响应式失效
   └── 标记脏节点

6. 布局
   └── 只重新布局脏子树

7. 动画插值
   ├── UI 动画
   ├── 游戏动画
   └── 相机动画

8. 生成渲染命令
   ├── game scene
   ├── UI tree
   ├── overlay
   └── debug layer

9. GPU pass
   ├── clear
   ├── scene pass
   ├── post-process
   ├── UI pass
   └── present

10. 统计帧时间 / 调试信息
```

---

## 6.2 固定时间步与渲染插值

游戏通常需要固定逻辑步长。

概念：

```text
accumulator += frame_dt

while accumulator >= fixed_dt:
    update_world(fixed_dt)
    accumulator -= fixed_dt

alpha = accumulator / fixed_dt
draw_world(alpha)
```

Racket 风格伪代码：

```racket
(define (tick-game! game-state frame-dt input)
  (set-game-accumulator! game-state
    (+ (game-accumulator game-state) frame-dt))

  (let loop ()
    (when (>= (game-accumulator game-state)
              (game-fixed-dt game-state))
      (update-world! (game-world game-state)
                     (game-fixed-dt game-state)
                     input)
      (set-game-accumulator! game-state
        (- (game-accumulator game-state)
           (game-fixed-dt game-state)))
      (loop))))
```

然后：

```racket
(define alpha
  (/ (game-accumulator game-state)
     (game-fixed-dt game-state)))

(draw-world! ctx world alpha)
```

---

# 7. 基本绘制接口

这是你最需要的部分之一。

建议提供一个统一的 `draw-context`。

---

## 7.1 DrawContext

```racket
(struct draw-context
  (backend
   state
   camera
   transform-stack
   clip-stack
   layer-stack))
```

所有绘制函数都接受 `ctx`。

---

## 7.2 基础状态控制

```racket
(with-transform ctx transform body ...)
(with-translation ctx pos body ...)
(with-rotation ctx angle body ...)
(with-scale ctx scale body ...)
(with-clip ctx shape body ...)
(with-layer ctx #:opacity o #:blend mode body ...)
(with-camera ctx camera body ...)
```

示例：

```racket
(with-translation ctx (vec2 100 50)
  (with-rotation ctx (* pi 0.25)
    (draw-rect ctx
      #:rect (rect 0 0 80 80)
      #:color (rgba 1 0 0 1))))
```

---

## 7.3 基础形状

```racket
(draw-rect ctx
  #:rect r
  #:color c
  #:radius [0]
  #:border [border-none]
  #:material [default-rect-material]
  #:uv [uv-full])

(draw-rounded-rect ctx
  #:rect r
  #:radius radius
  #:color c)

(draw-circle ctx
  #:center p
  #:radius r
  #:color c)

(draw-line ctx
  #:from a
  #:to b
  #:width w
  #:color c)

(draw-polyline ctx
  #:points points
  #:width w
  #:color c
  #:closed? [?])

(draw-triangle ctx
  #:points '(a b c)
  #:color c)

(draw-quad ctx
  #:points '(a b c d)
  #:color c)
```

---

## 7.4 文本

```racket
(draw-text ctx
  #:text str
  #:pos p
  #:font font
  #:size px
  #:color c
  #:align [left]
  #:line-height [1.2])

(draw-text-layout ctx
  #:layout text-layout
  #:pos p
  #:color c)
```

更高级：

```racket
(measure-text str #:font f #:size px #:max-width w)
(make-text-layout str #:font f #:size px #:max-width w)
```

---

## 7.5 图片 / 纹理

```racket
(draw-image ctx
  #:texture tex
  #:rect r
  #:color [white]
  #:uv [uv-full])

(draw-image-rotated ctx
  #:texture tex
  #:center c
  #:size s
  #:rotation angle
  #:color [white])
```

---

## 7.6 Sprite

游戏常用：

```racket
(draw-sprite ctx
  #:texture tex
  #:pos p
  #:size s
  #:origin [center]
  #:rotation [0]
  #:color [white]
  #:flip-x [#f]
  #:flip-y [#f]
  #:material [sprite-material]
  #:uniforms [#hash()])
```

---

## 7.7 Mesh

```racket
(draw-mesh ctx
  #:mesh mesh
  #:material material
  #:transform [mat3-identity]
  #:uniforms [#hash()])
```

---

## 7.8 Instancing

大量精灵、粒子建议用：

```racket
(draw-instanced ctx
  #:mesh mesh
  #:material material
  #:instance-buffer buffer
  #:instance-count n)
```

---

## 7.9 Render target

```racket
(with-render-target ctx target
  body ...)

(with-offscreen ctx #:size s #:format fmt
  body ...)
```

用于：

- 后处理；
- 阴影；
- 光照；
- 离屏 UI；
- 缩略图；
- 屏幕反射。

---

## 7.10 Debug 绘制

游戏开发很需要：

```racket
(draw-debug-rect ctx rect #:color c)
(draw-debug-circle ctx center radius #:color c)
(draw-debug-line ctx a b #:color c)
(draw-debug-grid ctx cell-size #:color c)
(draw-debug-collider ctx collider #:color c)
(draw-debug-text ctx pos str #:color c)
```

---

# 8. 基础布局系统

UI 部分需要布局容器。

---

## 8.1 常见布局容器

```racket
(vstack #:spacing #:align #:padding children ...)
(hstack #:spacing #:align #:padding children ...)
(zstack #:align children ...)
(grid #:columns #:rows #:gap children ...)
(scroll-view #:horizontal? #:vertical? child)
(list-view #:items items #:key key-f #:render render-f)
(padding #:all #:top #:right #:bottom #:left child)
(center child)
(expand child)
(spacer #:size s)
(aspect-ratio #:ratio r child)
(clip child)
```

---

## 8.2 布局属性

每个元素可接受：

```racket
#:width
#:height
#:min-width
#:max-width
#:min-height
#:max-height
#:padding
#:margin
#:grow
#:shrink
#:align
#:justify
```

例如：

```racket
(hstack #:spacing 8
  (text "Name:" #:width 80)
  (text-input #:value name #:grow 1))
```

---

## 8.3 Flexbox 风格属性

推荐至少支持：

```text
flex-grow
flex-shrink
flex-basis
align-self
justify-content
align-items
gap
```

示例：

```racket
(hstack #:justify 'space-between #:align 'center
  (text "Score")
  (text score-text))
```

---

# 9. 基础元素组件

框架应该提供一组基础组件。

---

## 9.1 基础显示元素

```racket
(rect ...)
(rounded-rect ...)
(text ...)
(image ...)
(icon ...)
(border ...)
(divider ...)
(spacer ...)
(canvas ...)
(gpu-view ...)
(shader-view ...)
```

---

## 9.2 基础交互元素

```racket
(button ...)
(toggle ...)
(checkbox ...)
(radio ...)
(slider ...)
(text-input ...)
(password-input ...)
(dropdown ...)
(menu ...)
(menu-item ...)
(popup ...)
(tooltip ...)
(dialog ...)
```

---

## 9.3 复合组件

```racket
(card ...)
(list-item ...)
(table ...)
(tree-view ...)
(tab-view ...)
(nav-sidebar ...)
(toolbar ...)
(status-bar ...)
(form-field ...)
(file-picker ...)
(color-picker ...)
```

---

## 9.4 游戏相关组件

```racket
(game-view ...)
(sprite-view ...)
(tilemap-view ...)
(camera-view ...)
(animation-view ...)
(hud ...)
(debug-overlay ...)
(minimap ...)
```

---

# 10. 声明式组件示例

下面是一个较完整的 UI 组件示例。

```racket
(define/component (signup-form)
  (define email (use-signal ""))
  (define password (use-signal ""))
  (define remember? (use-signal #f))

  (define email-valid?
    (signal-map email non-empty-string?))

  (card
    #:padding 16
    #:spacing 12
    (text "Sign Up" #:style 'title)

    (form-field "Email"
      (text-input
        #:value email
        #:placeholder "you@example.com"))

    (form-field "Password"
      (password-input
        #:value password))

    (checkbox
      #:value remember?
      #:label "Remember me")

    (button "Submit"
      #:enabled email-valid?
      #:on-click
      (lambda ()
        (submit-signup!
          (signal-ref email)
          (signal-ref password)
          (signal-ref remember?))))))
```

---

# 11. 游戏接口设计

游戏部分要单独设计。

---

## 11.1 `game-view`

推荐主接口：

```racket
(game-view
  #:init create-world
  #:destroy destroy-world!
  #:update update-world!
  #:draw draw-world!
  #:fixed-step [1/60]
  #:input-map [default-input-map]
  #:camera [default-camera]
  #:background-color [black])
```

---

## 11.2 世界创建

```racket
(struct world
  (camera
   player
   enemies
   bullets
   tilemap
   time
   paused?
   hp-signal
   score-signal)
  #:mutable)

(define (create-world ctx)
  (world
    (make-camera-2d)
    (create-player)
    '()
    '()
    (load-tilemap "level1.tmx")
    0.0
    #f
    (signal 100)
    (signal 0)))
```

---

## 11.3 更新世界

```racket
(define (update-world! w dt input)
  (unless (world-paused? w)
    (set-world-time! w (+ (world-time w) dt))

    (when (input-action-pressed? input 'pause)
      (set-world-paused?! w #t))

    (update-player! (world-player w) dt input)
    (update-enemies! w dt)
    (update-bullets! w dt)
    (resolve-collisions! w)

    (signal-set! (world-hp-signal w)
                 (player-hp (world-player w)))

    (signal-set! (world-score-signal w)
                 (world-score w))))
```

注意：

- `world` 内部大量状态是普通可变数据；
- 只有 `hp-signal`、`score-signal` 这类 UI 需要展示的数据才同步到 signal。

---

## 11.4 绘制世界

```racket
(define (draw-world! ctx w alpha)
  (with-camera ctx (world-camera w)
    (draw-tilemap ctx (world-tilemap w))

    (draw-player ctx (world-player w) alpha)

    (for ([e (world-enemies w)])
      (draw-enemy ctx e alpha))

    (for ([b (world-bullets w)])
      (draw-bullet ctx b alpha))))
```

---

## 11.5 在游戏中使用 shader

```racket
(define (draw-player ctx player alpha)
  (draw-sprite ctx
    #:texture (player-texture player)
    #:pos (lerp-pos (player-prev-pos player)
                    (player-pos player)
                    alpha)
    #:size (player-size player)
    #:rotation (player-rotation player)
    #:material player-material
    #:uniforms
    (hash
      'u-time (current-time)
      'u-hit-flash (player-hit-flash player))))
```

---

## 11.6 游戏与 UI 的桥接

推荐在游戏世界中保留少量 signal：

```racket
(world-hp-signal w)
(world-score-signal w)
(world-paused-signal w)
```

然后 UI 直接读取：

```racket
(define/component (hud w)
  (zstack
    #:align 'top-left
    (hstack
      (text "HP:")
      (progress-bar
        #:value (signal-map (world-hp-signal w)
                            (lambda (hp) (/ hp 100.0)))))

    (text
      (signal-map (world-score-signal w)
                  (lambda (s) (format "Score: ~a" s))))))
```

UI 发送命令给游戏：

```racket
(button "Pause"
  #:on-click (lambda ()
               (toggle-pause! world)))
```

或者使用事件通道：

```racket
(send-game-event! world 'pause)
```

---

# 12. 输入系统设计

你需要两套输入接口：

1. 原始输入；
2. 动作映射。

---

## 12.1 原始输入

```racket
(key-down? input 'space)
(key-pressed? input 'space)
(key-released? input 'space)

(mouse-pos input)
(mouse-down? input 'left)
(mouse-pressed? input 'left)
(mouse-wheel-delta input)

(gamepad-axis input 0 'left-stick-x)
(gamepad-down? input 0 'a)
```

---

## 12.2 动作映射

游戏最好不要直接绑按键，而是绑动作。

```racket
(define-action-map game-input-map
  (move-left key:a key:left gamepad:left-stick-left)
  (move-right key:d key:right gamepad:left-stick-right)
  (jump key:space gamepad:button-a)
  (pause key:esc gamepad:start)
  (fire mouse:left gamepad:right-trigger))
```

更新中使用：

```racket
(input-action-held? input 'move-left)
(input-action-pressed? input 'jump)
(input-action-value input 'fire)
```

---

# 13. Shader / Material 支持

这是重点。

---

## 13.1 设计目标

Shader 系统需要支持：

- 顶点着色器；
- 片元着色器；
- compute shader；
- uniform；
- texture binding；
- sampler；
- blend mode；
- depth/stencil；
- instancing；
- render target；
- post-process；
- shader hot reload。

---

## 13.2 Material 是核心抽象

不要把 shader 直接暴露给每个绘制函数。

建议抽象成：

```text
Shader -> Pipeline -> Material -> DrawCall
```

用户大多数时候面对的是 `material`。

---

## 13.3 Material 数据结构

```racket
(struct material
  (id
   pipeline
   uniforms
   texture-slots
   blend-mode
   depth-mode
   cull-mode)
  #:transparent)
```

---

## 13.4 Shader 资源

```racket
(struct shader-module
  (stage
   path
   entry
   compiled)
  #:mutable)
```

`stage` 可以是：

```racket
'vertex
'fragment
'compute
```

---

## 13.5 Uniform 描述

```racket
(struct uniform-slot
  (name
   type
   default-value)
  #:transparent)
```

类型：

```text
f32
vec2
vec3
vec4
mat3
mat4
texture2d
sampler
```

---

## 13.6 定义 material

推荐 DSL：

```racket
(define/material sprite-material
  (material
    #:vertex "shaders/sprite.vert"
    #:fragment "shaders/sprite.frag"
    #:blend 'alpha
    #:uniforms
    (list
      (uniform-slot 'u-color 'vec4 (vec4 1 1 1 1))
      (uniform-slot 'u-time 'f32 0.0))
    #:textures
    (list
      (texture-slot 'u-texture))))
```

---

## 13.7 使用 material

在 UI 中：

```racket
(rect
  #:width 200
  #:height 100
  #:material dissolve-material
  #:uniforms
  (hash
    'u-progress progress-signal
    'u-noise noise-texture))
```

在游戏中：

```racket
(draw-sprite ctx
  #:texture player-tex
  #:pos player-pos
  #:size player-size
  #:material dissolve-material
  #:uniforms
  (hash
    'u-progress 0.4
    'u-noise noise-tex))
```

---

## 13.8 内置 material

框架应内置：

```racket
solid-color-material
texture-2d-material
sprite-material
ui-rounded-rect-material
ui-text-material
ui-border-material
line-material
particle-material
post-process-copy-material
```

---

## 13.9 UI 圆角矩形 shader

例如：

```racket
(define/material ui-rounded-rect
  (material
    #:vertex "shaders/ui_rect.vert"
    #:fragment "shaders/ui_rounded_rect.frag"
    #:blend 'alpha
    #:uniforms
    (list
      (uniform-slot 'u-color 'vec4 (vec4 1 1 1 1))
      (uniform-slot 'u-radius 'f32 0.0)
      (uniform-slot 'u-border-width 'f32 0.0)
      (uniform-slot 'u-border-color 'vec4 (vec4 0 0 0 1)))))
```

绘制：

```racket
(draw-rounded-rect ctx
  #:rect (rect 0 0 200 100)
  #:radius 12
  #:color (rgba 0.2 0.5 1 1))
```

底层实际会调用该 material。

---

## 13.10 文本 shader

文本推荐：

- MSDF / SDF text；
- glyph atlas；
- 支持缩放；
- 支持 subpixel / anti-aliasing。

Material：

```racket
(define/material ui-text
  (material
    #:vertex "shaders/ui_text.vert"
    #:fragment "shaders/ui_text_msdf.frag"
    #:blend 'alpha
    #:textures
    (list (texture-slot 'u-glyph-atlas))
    #:uniforms
    (list
      (uniform-slot 'u-color 'vec4 (vec4 1 1 1 1))
      (uniform-slot 'u-px-range 'f32 4.0))))
```

---

## 13.11 Sprite shader

```racket
(define/material sprite
  (material
    #:vertex "shaders/sprite.vert"
    #:fragment "shaders/sprite.frag"
    #:blend 'alpha
    #:textures
    (list (texture-slot 'u-texture))
    #:uniforms
    (list
      (uniform-slot 'u-color 'vec4 (vec4 1 1 1 1))
      (uniform-slot 'u-time 'f32 0.0))))
```

---

## 13.12 Post-process shader

后处理接口：

```racket
(post-process
  #:shader "shaders/crt.frag"
  #:uniforms
  (hash
    'u-time time-signal
    'u-intensity 0.7))
```

或者在 pipeline 中：

```racket
(define-pipeline game-pipeline
  (pass #:clear)
  (pass #:scene)
  (pass #:post "shaders/crt.frag")
  (pass #:ui)
  (pass #:present))
```

---

## 13.13 Compute shader

如果支持 GPU 粒子：

```racket
(compute-pass
  #:shader "shaders/particles.comp"
  #:dispatch (groups-x groups-y groups-z)
  #:bindings
  (hash
    'particle-buffer particle-buffer
    'dt dt))
```

---

## 13.14 Shader 编译策略

建议：

```text
用户源码
├── WGSL
├── GLSL
└── HLSL
        ↓
shader compiler / reflection
        ↓
SPIR-V / MSL / DXIL
        ↓
GPU backend
```

实际可以通过：

- naga；
- shaderc；
- glslang；
- spirv-cross；
- wgpu-native。

在 Racket 侧最好只维护声明式绑定：

```racket
(uniform-slot 'u-time 'f32 0)
(texture-slot 'u-texture)
```

避免完全依赖运行时反射。

---

# 14. 顶层定制方式

你需要提供多层定制能力。

---

## 14.1 应用级配置

```racket
(define/app (main)
  (app
    #:window
    (window
      #:title "My Game"
      #:size (size 1280 720)
      #:min-size (size 800 600)
      #:resizable? #t
      #:fullscreen? #f
      #:vsync? #t)

    #:renderer
    (renderer
      #:backend 'auto
      #:msaa 4
      #:clear-color (rgb 0.05 0.06 0.08)
      #:frame-limit 120)

    #:theme dark-theme
    #:pipeline default-pipeline
    #:input-map global-input-map

    (root-screen)))
```

---

## 14.2 窗口配置

```racket
(window
  #:title
  #:size
  #:position
  #:min-size
  #:max-size
  #:resizable?
  #:fullscreen?
  #:decorated?
  #:transparent?
  #:scale-factor
  #:icon)
```

---

## 14.3 渲染器配置

```racket
(renderer
  #:backend 'auto | 'wgpu | 'gl | 'metal | 'vulkan
  #:msaa 1 | 2 | 4 | 8
  #:vsync? #t
  #:clear-color c
  #:frame-limit fps
  #:debug? #t
  #:texture-filter 'linear
  #:default-font "Inter"
  #:default-font-size 14)
```

---

## 14.4 主题定制

```racket
(define-theme dark-theme
  (theme
    #:color-background (rgb 0.07 0.08 0.10)
    #:color-surface (rgb 0.12 0.13 0.16)
    #:color-primary (rgb 0.23 0.51 0.96)
    #:color-danger (rgb 0.91 0.30 0.24)
    #:color-text (rgb 0.93 0.94 0.95)
    #:font-default "Inter"
    #:font-mono "JetBrains Mono"
    #:radius-sm 4
    #:radius-md 8
    #:radius-lg 12
    #:spacing-xs 4
    #:spacing-sm 8
    #:spacing-md 12
    #:spacing-lg 16

    #:component button
    (component-style
      #:padding (insets 8 12)
      #:radius (theme 'radius-md)
      #:background (theme-color 'primary)
      #:foreground (theme-color 'text)
      #:hover (style #:opacity 0.9)
      #:active (style #:opacity 0.8)
      #:disabled (style #:opacity 0.5))

    #:component text-input
    (component-style
      #:padding (insets 6 10)
      #:radius (theme 'radius-sm)
      #:background (theme-color 'surface)
      #:border (border 1 (theme-color 'border))
      #:focused (style #:border (border 1 (theme-color 'primary))))))
```

---

## 14.5 Pipeline 定制

```racket
(define-pipeline default-pipeline
  (pass #:clear)
  (pass #:game #:layer 'world)
  (pass #:post-process #:enabled post-fx?)
  (pass #:ui #:layer 'ui)
  (pass #:debug #:enabled debug?)
  (pass #:present))
```

也可以提供函数式钩子：

```racket
(renderer
  #:pre-render on-pre-render
  #:post-render on-post-render
  #:before-ui on-before-ui
  #:after-ui on-after-ui)
```

---

## 14.6 自定义元素

```racket
(define-element progress-bar
  #:props
  (value
   color
   background-color
   radius)

  #:measure
  (lambda (props constraints)
    (size 200 12))

  #:draw
  (lambda (ctx props box)
    (define r (rect 0 0 (box-width box) (box-height box)))
    (draw-rounded-rect ctx
      #:rect r
      #:radius (progress-bar-radius props)
      #:color (progress-bar-background-color props))

    (define fill-width
      (* (box-width box)
         (clamp01 (progress-bar-value props))))

    (draw-rounded-rect ctx
      #:rect (rect 0 0 fill-width (box-height box))
      #:radius (progress-bar-radius props)
      #:color (progress-bar-color props))))
```

使用：

```racket
(progress-bar #:value 0.42 #:color (rgb 0.2 0.8 0.4))
```

---

## 14.7 自定义布局

```racket
(define-layout radial-layout
  #:props
  (radius start-angle gap-angle)

  #:measure
  (lambda (props constraints children)
    (size (* 2 (radial-layout-radius props))
          (* 2 (radial-layout-radius props))))

  #:place
  (lambda (props box children)
    (for/list ([child children]
               [i (in-naturals)])
      (define angle
        (+ (radial-layout-start-angle props)
           (* i (radial-layout-gap-angle props))))

      (define pos
        (vec2 (+ (rect-x box)
                 (rect-w box)
                 (* (radial-layout-radius props) (cos angle)))
              (+ (rect-y box)
                 (rect-h box)
                 (* (radial-layout-radius props) (sin angle)))))

      (placement child #:pos pos #:anchor 'center))))
```

这允许用户做非常规布局。

---

## 14.8 自定义组件

```racket
(define/component (user-card user)
  (card
    (hstack
      (image (user-avatar user) #:size 48 #:radius 24)
      (vstack
        (text (user-name user) #:style 'title)
        (text (user-email user) #:style 'caption)))))
```

---

# 15. 游戏相关补充接口

除了绘制，还需要这些。

---

## 15.1 相机

```racket
(make-camera-2d)
(camera-2d-center cam)
(camera-2d-zoom cam)
(camera-2d-follow cam target #:smooth 0.1)
(screen->world cam screen-pos)
(world->screen cam world-pos)
```

3D：

```racket
(make-camera-3d)
(camera-3d-position cam)
(camera-3d-target cam)
(camera-3d-up cam)
(camera-3d-fov cam)
```

---

## 15.2 精灵动画

```racket
(struct sprite-animation
  (frames
   fps
   loop?
   current-frame
   time))

(make-sprite-animation texture #:frames 8 #:fps 12 #:loop? #t)
(update-animation! anim dt)
(animation-current-frame anim)
```

绘制：

```racket
(draw-animation ctx anim #:pos p #:size s)
```

---

## 15.3 Tilemap

```racket
(load-tilemap path)
(draw-tilemap ctx tilemap)
(tilemap-world->tile tm pos)
(tilemap-tile->world tm x y)
(tilemap-collision? tm world-pos)
```

---

## 15.4 碰撞

基础接口：

```racket
(aabb-overlap? a b)
(circle-overlap? c1 c2)
(ray-rect-intersect ray rect)
(resolve-aabb-collision mover obstacle)
```

高级一点：

```racket
(struct collider (shape offset layer mask))
(PhysicsWorld)
(physics-step! pw dt)
(physics-query-point pw pos)
(physics-raycast pw from to)
```

---

## 15.5 粒子

```racket
(struct particle-system
  (emitter
   particles
   material
   buffer))

(update-particles! ps dt)
(draw-particles ctx ps)
```

GPU 粒子：

```racket
(gpu-particle-system
  #:compute "particles.comp"
  #:max-particles 100000
  #:material particle-material)
```

---

## 15.6 Audio

游戏开发通常也需要音频：

```racket
(load-audio "hit.ogg")
(play-sound! sfx)
(play-music! bgm #:loop? #t)
(set-master-volume! v)
(set-listener-position! pos)
(play-positional! sfx pos)
```

---

# 16. ECS 接口建议

如果你要支持中大型游戏，可以考虑提供可选 ECS。

---

## 16.1 组件定义

```racket
(define-component position (x y))
(define-component velocity (dx dy))
(define-component sprite (texture size tint rotation))
(define-component health (hp))
(define-component player ())
(define-component enemy ())
```

---

## 16.2 系统定义

```racket
(define-system movement-system
  #:query (position velocity)
  #:stage 'update
  (lambda (e pos vel dt)
    (set-position-x! pos
      (+ (position-x pos)
         (* (velocity-dx vel) dt)))
    (set-position-y! pos
      (+ (position-y pos)
         (* (velocity-dy vel) dt)))))
```

---

## 16.3 查询

```racket
(query-entities world (position sprite))
```

---

## 16.4 渲染系统

```racket
(define-system sprite-render-system
  #:query (position sprite)
  #:stage 'draw
  (lambda (ctx e pos spr)
    (draw-sprite ctx
      #:texture (sprite-texture spr)
      #:pos (vec2 (position-x pos) (position-y pos))
      #:size (sprite-size spr)
      #:rotation (sprite-rotation spr)
      #:color (sprite-tint spr))))
```

---

# 17. 你可能遗漏的接口列表

下面这些非常容易被忽略，但很重要。

---

## 17.1 平台与窗口

需要：

```text
窗口创建
窗口销毁
最小化 / 最大化
全屏切换
DPI 缩放
窗口大小变化
多显示器
窗口焦点
鼠标捕获
光标样式
拖拽文件
剪贴板
系统通知
生命周期暂停/恢复
```

接口示例：

```racket
(set-window-title! w title)
(set-window-size! w size)
(set-fullscreen! w #t)
(request-attention w)
(screen-scale-factor screen)
(mouse-cursor-set! 'ibeam)
```

---

## 17.2 输入

需要：

```text
键盘
鼠标
触摸
手柄
滚轮
手势
拖拽
快捷键
输入法
文本编辑事件
按键重复
```

接口示例：

```racket
(key-repeat? input key)
(start-text-input!)
(stop-text-input!)
(ime-composition-string input)
(touch-points input)
(gesture-pinch-delta input)
```

---

## 17.3 文本 / 字体

需要：

```text
字体加载
字体回退
字形整形
换行
对齐
省略号
选择
光标移动
输入法组合文本
富文本
MSDF/SDF 渲染
多语言
bidi
emoji
```

接口示例：

```racket
(load-font path)
(make-text-layout str #:font f #:size px #:max-width w)
(hit-test-text-layout layout pos)
(text-layout-caret-pos layout index)
```

---

## 17.4 焦点与可访问性

需要：

```text
焦点树
Tab 导航
方向键导航
焦点环
屏幕阅读器
语义角色
按钮 / 输入框 / 列表状态
高对比度
减少动画偏好
```

接口示例：

```racket
(focus-next!)
(focus-previous!)
(set-focused-node! id)
(accessibility-label node "Submit form")
(accessibility-role node 'button)
```

---

## 17.5 资源管理

需要：

```text
纹理缓存
字体缓存
音频缓存
着色器缓存
异步加载
加载进度
资源引用计数
资源释放
热重载
压缩纹理
图集打包
```

接口示例：

```racket
(load-asset path)
(load-asset-async path on-ready on-error)
(asset-cache-get key loader)
(release-asset! key)
(reload-asset! key)
```

组件中使用：

```racket
(define tex (use-asset "player.png"))
(define font (use-font "Inter.ttf" #:size 16))
```

---

## 17.6 动画系统

需要：

```text
tween
spring
keyframes
timeline
easing
loop
delay
repeat
animation state machine
skeletal animation
UI transition
shared element transition
```

接口示例：

```racket
(animate-signal! s target #:duration 0.3 #:easing ease-out)
(spring! s target #:stiffness 170 #:damping 26)
(keyframes #:duration 1.0
  (0.0 0)
  (0.5 1)
  (1.0 0.8))
```

---

## 17.7 渲染高级功能

需要：

```text
render target
mipmap
MSAA
scissor
stencil
depth
blend mode
screenshot
离屏渲染
层级缓存
脏矩形
occlusion culling
frustum culling
instancing
indirect draw
GPU timeline
```

接口示例：

```racket
(capture-screenshot path)
(set-blend-mode ctx 'additive)
(with-stencil ctx ...)
(make-render-target #:width w #:height h #:format 'rgba8)
```

---

## 17.8 性能分析

需要：

```text
帧时间
CPU 时间
GPU 时间
draw call 数量
batch 数量
纹理内存
顶点内存
布局耗时
信号更新耗时
组件重渲染统计
shader 编译耗时
```

接口示例：

```racket
(profiler-begin-frame!)
(profiler-end-frame!)
(show-profiler-window!)
(gpu-timestamp-query ...)
```

---

## 17.9 开发工具

需要：

```text
UI inspector
scene inspector
signal debugger
theme editor
shader hot reload
asset browser
layout bounds display
draw command viewer
frame debugger
```

例如：

```racket
(show-inspector!)
(show-theme-editor!)
(reload-shaders!)
```

---

## 17.10 序列化 / 场景保存

游戏开发常需要：

```text
保存关卡
保存预制体
保存动画
保存主题
保存输入绑定
保存 UI 布局
保存回放
```

接口示例：

```racket
(save-scene scene path)
(load-scene path)
(save-input-map input-map path)
```

---

## 17.11 错误与恢复

需要：

```text
GPU device lost
shader compile error
texture load error
font fallback
audio device failure
资源缺失占位符
帧异常恢复
日志系统
```

接口示例：

```racket
(with-fallback-texture missing-tex
  (draw-image ctx tex ...))

(on-shader-error
 (lambda (err)
   (log-error err)))
```

---

# 18. 推荐的核心 API 清单

下面是一个可以作为框架公开接口的清单。

---

## 18.1 应用

```racket
define/app
app
window
renderer
run-app
quit-app
request-redraw
```

---

## 18.2 组件

```racket
define/component
use-state
use-signal
use-memo
use-effect
use-resource
use-context
provide-context
```

---

## 18.3 Signal

```racket
signal
signal-ref
signal-set!
signal-update!
signal-map
signal-combine
derived
```

---

## 18.4 布局

```racket
vstack
hstack
zstack
grid
scroll-view
list-view
padding
center
expand
spacer
aspect-ratio
clip
```

---

## 18.5 基础元素

```racket
rect
rounded-rect
text
image
icon
button
text-input
checkbox
toggle
slider
dropdown
popup
dialog
canvas
gpu-view
```

---

## 18.6 绘制

```racket
draw-rect
draw-rounded-rect
draw-circle
draw-line
draw-polyline
draw-text
draw-image
draw-sprite
draw-mesh
draw-instanced
with-transform
with-clip
with-layer
with-camera
with-render-target
```

---

## 18.7 游戏

```racket
game-view
define/action-map
input-action-pressed?
input-action-held?
input-action-value
make-camera-2d
camera-2d-follow
draw-tilemap
load-tilemap
sprite-animation
update-animation!
draw-animation!
```

---

## 18.8 Shader / Material

```racket
define/material
material
uniform-slot
texture-slot
load-shader
make-pipeline
use-material
post-process
compute-pass
```

---

## 18.9 资源

```racket
load-texture
load-font
load-audio
load-shader
load-asset
use-asset
use-texture
use-font
release-asset!
```

---

## 18.10 主题

```racket
define/theme
theme
theme-color
theme-font
theme-radius
theme-spacing
component-style
style
with-theme
```

---

# 19. 一个完整示例：游戏 + HUD + Shader

下面是一个理想中的完整应用示例。

```racket
#lang racket
(require gpu-ui)

(define/app (main)
  (app
    #:window
    (window
      #:title "Scheme GPU Game"
      #:size (size 1280 720)
      #:vsync? #t)

    #:renderer
    (renderer
      #:backend 'auto
      #:msaa 4
      #:clear-color (rgb 0.04 0.05 0.07))

    #:theme dark-theme
    #:input-map global-input-map

    (root-screen)))
```

根界面：

```racket
(define/component (root-screen)
  (define world (use-world create-world destroy-world!))

  (zstack
    (game-view
      #:world world
      #:fixed-step 1/60
      #:update update-world!
      #:draw draw-world!
      #:input-map game-input-map)

    (hud world)

    (pause-overlay world)))
```

HUD：

```racket
(define/component (hud world)
  (padding 16
    (vstack #:spacing 8 #:align 'start
      (hstack #:spacing 8
        (text "HP" #:style 'label)
        (progress-bar
          #:width 200
          #:value
          (signal-map (world-hp-signal world)
                      (lambda (hp) (/ hp 100.0)))))

      (text
        (signal-map (world-score-signal world)
                    (lambda (score)
                      (format "Score: ~a" score)))
        #:style 'title))))
```

暂停层：

```racket
(define/component (pause-overlay world)
  (define paused? (world-paused-signal world))

  (when-view paused?
    (zstack
      (rect #:color (rgba 0 0 0 0.5))

      (card
        (text "Paused" #:style 'title)

        (hstack
          (button "Resume"
            #:on-click (lambda ()
                         (set-world-paused?! world #f)))

          (button "Quit"
            #:style 'danger
            #:on-click (lambda ()
                         (quit-app))))))))
```

世界定义：

```racket
(struct world
  (camera
   player
   enemies
   bullets
   tilemap
   time
   paused?
   hp-signal
   score-signal)
  #:mutable)

(define (create-world ctx)
  (define w
    (world
      (make-camera-2d)
      (create-player)
      (spawn-enemies 5)
      '()
      (load-tilemap "assets/level1.tmx")
      0.0
      #f
      (signal 100)
      (signal 0)))

  (camera-2d-follow! (world-camera w)
                     (world-player w)
                     #:smooth 0.12)

  w)

(define (destroy-world! w)
  (release-tilemap! (world-tilemap w)))
```

更新：

```racket
(define (update-world! w dt input)
  (when (input-action-pressed? input 'pause)
    (set-world-paused?! w (not (world-paused? w))))

  (unless (world-paused? w)
    (set-world-time! w (+ (world-time w) dt))

    (update-player! (world-player w) dt input)
    (update-enemies! w dt)
    (update-bullets! w dt)
    (resolve-collisions! w)

    (signal-set! (world-hp-signal w)
                 (player-hp (world-player w)))

    (signal-set! (world-score-signal w)
                 (world-score w))))
```

绘制：

```racket
(define (draw-world! ctx w alpha)
  (with-camera ctx (world-camera w)
    (draw-tilemap ctx (world-tilemap w))

    (draw-player ctx (world-player w) alpha)

    (for ([e (world-enemies w)])
      (draw-enemy ctx e alpha))

    (for ([b (world-bullets w)])
      (draw-bullet ctx b alpha))))
```

玩家绘制使用 shader：

```racket
(define/material player-material
  (material
    #:vertex "shaders/sprite.vert"
    #:fragment "shaders/player_flash.frag"
    #:blend 'alpha
    #:textures
    (list (texture-slot 'u-texture))
    #:uniforms
    (list
      (uniform-slot 'u-color 'vec4 (vec4 1 1 1 1))
      (uniform-slot 'u-time 'f32 0.0)
      (uniform-slot 'u-hit-flash 'f32 0.0))))

(define (draw-player ctx player alpha)
  (draw-sprite ctx
    #:texture (player-texture player)
    #:pos (lerp-vec2 (player-prev-pos player)
                     (player-pos player)
                     alpha)
    #:size (player-size player)
    #:rotation (player-rotation player)
    #:material player-material
    #:uniforms
    (hash
      'u-time (current-time)
      'u-hit-flash (player-hit-flash player))))
```

---

# 20. 设计思路总结

最后我把关键设计思路再压缩一下。

---

## 20.1 上层模型

```text
UI = f(state)
Game = update(world, dt, input) + draw(ctx, world, alpha)
```

两者统一在同一个应用框架中。

---

## 20.2 状态模型

```text
UI state -> signal
Game state -> world / ECS
Bridge -> signal / event channel
```

---

## 20.3 渲染模型

```text
声明式节点
  ↓
布局 / 场景树
  ↓
draw commands
  ↓
batching
  ↓
GPU pipelines
```

---

## 20.4 Shader 模型

```text
shader source
  ↓
pipeline
  ↓
material
  ↓
uniforms / textures
  ↓
draw call
```

---

## 20.5 游戏模型

```text
world
├── update(dt, input)
├── systems
├── entities / scene graph
├── camera
└── draw(ctx, alpha)
```

---

## 20.6 Scheme/Racket 的优势使用方式

- 宏负责 DSL；
- 函数负责运行时；
- signal 负责响应式；
- delimited continuation 负责流程；
- 参数 / context 负责环境；
- 结构体负责数据；
- FFI / unsafe 负责 GPU 高性能层。

---

# 21. 推荐实施顺序

如果你要真的开始实现，我建议这样排序。

---

## 第一阶段：最小 GPU 渲染器

目标：

```racket
(draw-rect ctx ...)
(draw-text ctx ...)
```

实现：

- 窗口；
- 输入；
- GPU backend；
- 基础 shader；
- 矩形；
- 文本；
- 图片。

---

## 第二阶段：基础 UI 树

目标：

```racket
(vstack
  (text "Hello")
  (button "Click"))
```

实现：

- 元素树；
- 布局；
- 命中测试；
- 基础事件；
- 按钮。

---

## 第三阶段：响应式状态

目标：

```racket
(define count (use-signal 0))
```

实现：

- signal；
- 依赖追踪；
- 脏节点；
- 局部更新。

---

## 第四阶段：组件系统

目标：

```racket
(define/component (counter) ...)
```

实现：

- 组件身份；
- slot table；
- use-state；
- use-effect；
- 热重载。

---

## 第五阶段：游戏视图

目标：

```racket
(game-view ...)
```

实现：

- world；
- update；
- draw；
- camera；
- input action；
- fixed timestep。

---

## 第六阶段：Material / Shader 系统

目标：

```racket
(define/material ...)
```

实现：

- pipeline；
- uniforms；
- textures；
- post-process；
- shader hot reload。

---

## 第七阶段：高级功能

包括：

- ECS；
- tilemap；
- 粒子；
- 物理；
- 音频；
- inspector；
- profiler；
- accessibility；
- 多窗口；
- 序列化。

---

# 22. 最简结论

如果你要一个最精炼的架构决策，就是：

> **做一个“声明式 UI + 游戏世界 + 统一 GPU 渲染管线”的混合框架。**

具体来说：

```text
UI 层：
SwiftUI / Compose 风格

状态层：
细粒度 signal + component slot

游戏层：
world / update / draw / camera / input actions
可选 ECS

渲染层：
draw context + display list + batching + GPU pipeline

Shader 层：
material 为核心，支持 uniforms / textures / post-process / compute

定制层：
theme / pipeline / custom element / custom layout / custom shader
```

这套架构既能支持现代应用 UI，也能支持实时游戏，还能充分利用 Scheme / Racket 的宏和抽象能力。
