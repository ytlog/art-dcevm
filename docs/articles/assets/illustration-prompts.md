# Article illustration

Generated with the built-in `imagegen` tool. The illustration describes the
architecture; it is not a device trace or verification evidence.

## Runtime architecture

```text
Use case: infographic-diagram
Asset type: refined architecture illustration for a Chinese technical blog about ART DCEVM.
Primary request: Draw an elegant, publication-quality overview explaining how ART DCEVM enhances the standard ART JVMTI RedefineClasses path. Create a complete flat infographic image, landscape 4:3, with crisp readable typography, generous whitespace and subtle isometric glass/acrylic accents. Match an editorial technical blog: warm ivory #fffefa background, deep ink #172c30 text, teal #087e80, muted mint #d4eee5, delicate warm gold accents, very restrained shadows, precise connectors. Make the engineering flow dominant, not decorative.
Composition: a vertical pipeline of five wide beautifully crafted modules, linked by clear downward arrows, inside a thin rounded container marked "应用进程". Each module has one large exact title and one brief exact subtitle. Small distinct geometric symbols evoke DEX pages, agent interface, an enhancement adapter, runtime engine, and four coordinated state tiles. Top header exact "ART DCEVM" with subheading exact "增强标准 RedefineClasses".
Module 1 title: "补丁工具" subtitle: "生成新 DEX"
Module 2 title: "用户 JVMTI Agent" subtitle: "RedefineClasses"
Module 3 title: "ART DCEVM" subtitle: "Hook · 校准 · 结构事务" (visually emphasize this module in teal).
Module 4 title: "设备 ART" subtitle: "分配 · 链接 · GC · 线程"
Module 5 title: "一致更新运行时状态" with four equal chips labelled exactly "类定义", "对象状态", "缓存引用", "执行帧".
Technical invariants: these are components inside ONE application process; the enhancement adapter uses actual device ART services and does not replace the system runtime. Arrows must follow the module order. This is a conceptual architecture illustration, not an execution trace. Do not add performance claims, compatibility badges, benchmark numbers, screenshots, code, extraneous labels, duplicated modules, robots or Android logos.
All specified text must be verbatim, easily legible and spelled correctly. No watermark.
```
