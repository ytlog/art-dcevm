# 调用图解释执行改造待完成项

旧 Android 13 后端及旧公共 AOT/JIT 私有提交引擎已删除。公开 JVMTI 路径可用；新调用图组件尚需接入完整私有事务。下面各项完成前，不能宣称完整结构替换或 Android 11–17 全部调通。

1. **完整调用图。** 已安装方法的真实 DEX、loader、声明和动态 dispatch 域需要经过解析后的公共 census 复核；`invoke-custom`、`invoke-polymorphic`、proxy 及尚未证明的 interface-super 目标仍明确拒绝。反射和 JNI 反射按当前开发范围延期。
2. **替换后的解释执行。** 已安装库存扫描仍只覆盖替换前的代码。`native_call_graph_incoming` 已能独立复制本次输入 DEX，读取目标 ClassDef 的全部 encoded methods 和真实新增调用，并按完整 DEX 内容、CodeItem 偏移和几何核验搬迁后的代码。`native_call_graph_replacement` 已从同一加载 Run 的实际 Holder 数组保留旧/新 Class 与 loader 根，把全部新声明按独立 incoming DEX 绑定到真实 ArtMethod，并在停止窗口重核。主机组合测试已实际激活旧/新方法解释入口、核验 retained set、扫描完整 ThreadList 并拒绝仍活动的新方法 quick 帧。两模块尚未接入 Execution 或生产 bootstrap，未提供完整动态分派域或提交权限；新类含 copied/default 方法时明确拒绝，仍需独立声明根与来源图的合并。接下来必须合并新旧图、解析完整目标域并接入原始事务生命周期，对新的上下游集合保持解释执行；手工组合选择集的主机通过不能代替完整调用图与设备提交证明。
3. **活动编译帧。** 在真实短暂停世界窗口激活逐方法解释入口，随后恢复线程。重新扫描全部注册线程和 inline 帧；若仍有选中的 quick 帧，明确拒绝。常驻启动帧未必退出，禁止无限等待或宣称入口已切换等于活动帧已切换。
4. **生产提交链替换。** 旧 RunState、CodeInfo 解码、boot AOT continuation、whole-process recovery 和编译代码失效流程已删除。新调用图事务需要独立实现并验证 GC 根替换、原生边界、缓存、初始化、monitor、DEX 发布和退款语义；当前私有初始化明确拒绝，不提供旧流程兜底。
5. **设备验证。** 新调用图模块的主机测试和严格 ARM64 编译属于开发验证。Android 16 的完整 schema 操作尚未成功；最终按 Android 11、12、13、14、15、16、17 / ARM64 逐台验证，不包含 12L、ARM32 或 x86。

原始测试日志和设备报告保存在忽略的 `build/evidence/`。必要结论、命令、设备与 APK 哈希记录在 [VERIFICATION.md](VERIFICATION.md)。
