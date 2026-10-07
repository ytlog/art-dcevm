# 单一增强库的链接边界

当前默认 NDK 构建在每个支持的 ABI 上生成一个增强库 `libartdcevm.so`。Android 13 / arm64 的完整演进代理与公开 JVMTI 后端都在这个库中；x86_64 使用公开后端。ShadowHook 由独立的 Maven/Prefab 依赖提供；debug 应用的测试支持库不属于生产演进引擎。这里的“单库”指增强引擎，不包括 Android 系统库、Hook 依赖和 debug 测试库。

`source_engine/`、AOSP 构建器与源码锁文件已删除。构建不下载或编译 AOSP，不把 AOSP 对象文件并入主库，不生成或装载 `libartdcevm_source13.so`，也不需要 `-PsourceEngine13`。固定私有声明来自已核对的 Android 13 源码，保存在自有 ABI 头中；这些声明不是待编译的 AOSP 实现。

## 运行时边界

`evolution_runtime` 在完整演进初始化时组织命名设备函数、校准与 Hook。标准 `jvmtiEnv::RedefineClasses` 是用户 Agent 的入口。需要改变行为时，增强库安装命名 Inline Hook 并执行自有代理；普通事务或仍需设备行为的步骤调用设备原函数或 trampoline。设备 ART 继续承担原生分配、类链接、堆收集和暂停等服务，增强库拥有演进校验、层级协调、字段迁移、结构提交、旧引用守卫与活动帧协调。

主库通过运行时命名 ELF 查找使用私有能力，不依赖加载时解析 ART 私有强符号。动态符号表及普通符号表均可用于命名查找；同库内部调用使用 Inline Hook。只有已证明所有目标调用点都经过导入表时，才适合使用 PLT Hook。函数地址与字段偏移不从反汇编推算。

初始化先严格核对 `libart.so`、`libopenjdkjvmti.so`、`libdexfile.so` 与平台 `libc++.so` 的 GNU Build ID，匹配固定声明 Profile 后才访问私有对象。编译器诊断另行匹配 `libart-compiler.so`。匹配不能替代行为校准：增强库使用公开 JNI/JVMTI 的真实类、字段、方法和 ID 观察，核对元数据、引用解码、字段存储、card table、monitor 与运行时服务；HandleScope 还通过命名设备 root visitor 核对真实 managed 根槽。平台字符串错误记录和 DEX 所有权分别通过真实设备调用检查。跨平台 libc++ 对象使用固定借用声明及设备析构，不把 NDK STL 对象传给设备函数。

其他 ART 构建需独立 Profile、符号、声明和设备回归。完整演进初始化失败时明确返回错误；应用可显式使用公开 JVMTI 后端，完整演进请求不自动退化为较弱操作。

## 验证状态与历史

2026-10-07 的无源码首版 APK `b051…` 已通过标准入口六种加载/并发/转换组合与完整 `source-first` 套件；后续最终回归仍在进行，最终 APK 哈希与验收结论以 [VERIFICATION.md](VERIFICATION.md) 为准。历史脚本名称、`source-first` 顺序名和 `build/evidence/source-engine/` 报告目录继续保留，它们不表示当前仍加载源码引擎。

早期独立源码 SO 曾直接引用 ART/JVMTI 和平台 libc++，不能仅把其对象并入 NDK 主库。迁移通过重新实现代理、命名绑定和自有校准移除了这个链接边界；旧 AOSP 归档与布局编译证据只保留在本地忽略的构建缓存和历史验证记录中，不是当前构建输入。
