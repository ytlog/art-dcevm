# 调用图解释执行改造待完成项

旧 Android 13 后端及旧公共 AOT/JIT 私有提交引擎已删除。公开 JVMTI 路径可用；新调用图组件尚需接入完整私有事务。下面各项完成前，不能宣称完整结构替换或 Android 11–17 全部调通。

1. **完整调用图。** 已安装方法的真实 DEX、loader、声明和动态 dispatch 域需要经过解析后的公共 census 复核；`invoke-custom`、`invoke-polymorphic`、proxy 及尚未证明的 interface-super 目标仍明确拒绝。反射和 JNI 反射按当前开发范围延期。
2. **替换后的解释执行。** `native_call_graph_incoming` 已独立复制本次输入 DEX，读取目标 ClassDef 的全部声明与真实新增调用，核验运行时 DEX 内容、CodeItem 偏移和几何。`native_call_graph_replacement::Batch` 已逐项绑定同一加载 Run 的实际 Holder 数组，要求提供的结构 partitions 内全部 Class 恰好覆盖一次，拒绝 Class/ArtMethod 重复及部分绑定。`Execution::PrepareReplacements` 已接入该批次；解释入口所有者在源加载暂停前校准，第二次真实停止窗口重核旧/新方法、激活新 Java 方法、复查两组 retained set，并分别扫描完整 ThreadList。无 Java 方法体的替换类无需伪造空选择集的线程扫描结果。`ReplacementInspectedDrained` 只描述已绑定方法体；完整新旧图闭包尚未建立，`compiled_drained` 保持 false，最终检查明确拒绝。新 outgoing 目标、动态 dispatch 域、copied/default 方法的独立声明根和来源图合并，以及生产 bootstrap 的原始事务生命周期仍须接入；主机组合测试不能代替完整图或设备提交证明。
3. **活动编译帧。** 在真实短暂停世界窗口激活逐方法解释入口，随后恢复线程。重新扫描全部注册线程和 inline 帧；若仍有选中的 quick 帧，明确拒绝。常驻启动帧未必退出，禁止无限等待或宣称入口已切换等于活动帧已切换。
4. **生产提交链替换。** 旧 RunState、CodeInfo 解码、boot AOT continuation、whole-process recovery 和编译代码失效流程已删除。新调用图事务需要独立实现并验证 GC 根替换、原生边界、缓存、初始化、monitor、DEX 发布和退款语义。`Execution::RefundReplacements` 已在同一 Run/Holder 的真实停止窗口接入本次新增解释项的原始 `Undeoptimize` 清理：完整扫栈后拒绝仍持有替换 Java 方法的 quick/shadow 帧，保留原有项，并支持部分激活或部分清理失败后的重试。它只证明本次原生集合项已清理；缓存、其他持有者和原始退款生命周期仍未接入，外部已有项必须继续保留 Class/元数据。仅清空 JNI Store 不能证明完整退款完成；当前私有初始化明确拒绝，不提供旧流程兜底。
5. **设备验证。** 新调用图模块的主机测试和严格 ARM64 编译属于开发验证。Android 16 的完整 schema 操作尚未成功；最终按 Android 11、12、13、14、15、16、17 / ARM64 逐台验证，不包含 12L、ARM32 或 x86。

原始测试日志和设备报告保存在忽略的 `build/evidence/`。必要结论、命令、设备与 APK 哈希记录在 [VERIFICATION.md](VERIFICATION.md)。
