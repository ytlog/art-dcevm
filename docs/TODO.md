# 调用图解释执行改造待完成项

旧 Android 13 后端及旧公共 AOT/JIT 私有提交引擎已删除。公开 JVMTI 路径可用；新调用图组件尚需接入完整私有事务。下面各项完成前，不能宣称完整结构替换或 Android 11–17 全部调通。

1. **完整调用图。** 已安装方法的真实 DEX、loader、声明和动态 dispatch 域需要经过解析后的公共 census 复核；`invoke-custom`、`invoke-polymorphic`、proxy 及尚未证明的 interface-super 目标仍明确拒绝。反射和 JNI 反射按当前开发范围延期。
2. **替换后的解释执行。** 现有库存扫描覆盖替换前的代码。必须加入真实 incoming DEX 的新增调用、替换 Class 的新 ArtMethod 根，并对新的上下游集合保持解释执行；旧方法进入 ART deoptimized set 不能证明新方法也在集合中。
3. **活动编译帧。** 在真实短暂停世界窗口激活逐方法解释入口，随后恢复线程。重新扫描全部注册线程和 inline 帧；若仍有选中的 quick 帧，明确拒绝。常驻启动帧未必退出，禁止无限等待或宣称入口已切换等于活动帧已切换。
4. **生产提交链替换。** 旧 RunState、CodeInfo 解码、boot AOT continuation、whole-process recovery 和编译代码失效流程已删除。新调用图事务需要独立实现并验证 GC 根替换、原生边界、缓存、初始化、monitor、DEX 发布和退款语义；当前私有初始化明确拒绝，不提供旧流程兜底。
5. **设备验证。** 新调用图模块的主机测试和严格 ARM64 编译属于开发验证。Android 16 的完整 schema 操作尚未成功；最终按 Android 11、12、13、14、15、16、17 / ARM64 逐台验证，不包含 12L、ARM32 或 x86。

原始测试日志和设备报告保存在忽略的 `build/evidence/`。必要结论、命令、设备与 APK 哈希记录在 [VERIFICATION.md](VERIFICATION.md)。
