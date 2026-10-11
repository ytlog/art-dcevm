# ART DCEVM 实验工程

目标是在 ARM64 Android 应用内实现类演进。当前开发范围为 Android 11 / API 30、12 / 31、13 / 33、14 / 34、15 / 35、16 / 36、17 / 37；不包含 Android 12L / API 32、ARM32 或 x86。

公开 JVMTI 后端提供方法体替换，并按运行时实际暴露的 ART 扩展提供新增成员能力。旧 `platform/android13` 后端和旧公共 AOT/JIT 私有提交引擎已清退；新的 `platform/common/native_call_graph_*` 组件正在接入完整私有事务。历史 Android 13 测试结果不代表当前重写版本已完成完整演进。

完整初始化 `DcevmRuntime.initialize(context)` 不能把部分能力初始化当作成功。完整生产契约尚未验证时，它返回不可用；公开代理可通过 `DcevmRuntime.attachPublicJvmti(context)` 附加。开发测试中的公共能力初始化单独报告可用子集。

## 当前架构

- `platform/common/native_call_graph*`：已安装方法的真实 DEX 调用点、跨 DEX/loader 解析、完整动态 dispatch 域、上下游闭包及一次操作的执行检查。
- `platform/common/native_selective_interpretation*`：通过 ART 命名 `Instrumentation::Deoptimize(ArtMethod*)` 切换选中方法的未来入口，并核验其 retained set。已经入栈的编译帧需要另外检查。
- `platform/common/native_call_graph_stack*`：在真实停世界窗口枚举全部注册线程及 inline 帧，报告选中方法的 quick/shadow 状态和独立的原生边界。
- `platform/common` 的其余模块：公共元数据校准、类层级、成员存储、根刷新、原始结构事务、发布和退款。已有提交路径的编译帧恢复部分仍在迁移，不能以新 stack 报告直接替代 GC 根替换证明。
- `platform/abi/arm64/android11` 至 `android17`：版本差异的固定 AOSP 源码声明。这些 ABI 声明仍需保留，与已删除的旧 Android 13 实现目录不同。
- `platform/symbols`、`hooks`：命名 ELF 符号和官方 ShadowHook。私有能力使用固定声明与独立运行时校准，不通过反汇编推算地址或字段偏移。

公共功能共享一个实现；只有真实声明和布局差异放入版本目录。调用图不完整、原始解析异常、代际变化或选中编译帧仍然存在时明确拒绝。反射和 JNI 反射按当前开发范围延期。

## 验证与开发

当前主要验证 Android 16；完整 schema 操作尚未调通。新调用图主机测试、源码声明检查和 ARM64 编译不能代替设备回归。后续工作见 [TODO.md](TODO.md)，必要验证记录见 [VERIFICATION.md](VERIFICATION.md)，版本矩阵见 [ANDROID_COMPATIBILITY.md](ANDROID_COMPATIBILITY.md)。

批量完成代码后再构建 Debug 应用、插桩 APK 和 Release AAR。设备运行库保存到工程 `assets/runtime_lib` 的分类目录供本机开发核对；运行库、构建产物和逐次报告不提交到 Git，原始证据保存在忽略的 `build/evidence/`。

项目背景与原理的历史图示见 [原理说明](articles/art-dcevm.html)。发布说明见 [MAVEN_PUBLISHING.md](MAVEN_PUBLISHING.md)。这些历史说明中的旧实现细节不作为当前重写路径的运行证明。
