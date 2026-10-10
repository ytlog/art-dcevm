# Core idea illustration

Generated with the built-in imagegen tool for the standalone article.

```text
Use case: infographic-diagram
Asset type: a precise, refined illustration for the "核心思路" subsection of a Chinese technical blog about ART DCEVM.
Primary request: Explain the division of responsibility: the project rewrites and supplements structural redefinition handling, while reusing ART's original redefinition transaction and runtime services. The standard user-facing JVMTI interface remains the entry point.
Style: premium editorial engineering infographic, wide 4:3 landscape. Warm ivory background #fffefa, deep ink #172c30, teal #087e80, pale mint #d4eee5, restrained warm gold #b68c40 connector accents. Subtle isometric depth, matte and lightly translucent modular panels, fine rules, soft shadows, ample whitespace. Crisp, large Chinese typography with a disciplined grid. Matching an elegant technical publication, not advertising.
Composition: three horizontal layers with a clear top-to-bottom flow. Titles and labels must be readable at article width. Avoid decorative side objects.
Top title exact: "ART DCEVM 的核心思路"
Subtitle exact: "扩展结构处理逻辑，复用 ART 原生能力"
Layer 1 is a slim ivory interface band: "JVMTI Agent" followed by a right arrow to "RedefineClasses". Small label exact: "标准调用入口".
A single downward connector to layer 2 is labelled exactly "Hook 接入".
Layer 2 is the central and visually dominant mint/teal panel. A small badge exact "项目实现". Title exact "ART DCEVM 结构增强逻辑". Inside, five equal modules in one row, each with a tasteful, technically relevant simple icon and one exact label:
"结构分析", "对象迁移", "旧引用处理", "活动帧检查", "批次协调".
Depict these as coordinated peer responsibilities, not five strict execution steps. No arrows between these five modules.
Below the whole panel, a short downward connector labelled exactly "调用与协调" connects to layer 3.
Layer 3 is a solid, neutral ivory/pale gray foundation panel, with a small badge exact "ART 提供". Title exact "原生事务与运行时服务". Inside, five equal modules with simple icons and exact labels:
"重定义事务", "对象分配", "类链接", "GC", "线程管理".
Use a clearly different neutral treatment for this foundation; it is preserved device ART, not a replacement implementation.
Technical invariants: the enhancement hooks into the standard RedefineClasses processing chain, extends structural handling, and reuses the real ART transaction/services. Do not depict replacement of the system runtime, a second JVM, a new standalone GC, partial batch commits, universal compatibility or verified-device badges. Arrows represent dependency and delegation, not an exact internal call trace.
Render only the specified text, no extra captions or paragraphs, no code fragments, no watermark. Exact spelling: ART DCEVM, JVMTI Agent, RedefineClasses, Hook, GC.
```
