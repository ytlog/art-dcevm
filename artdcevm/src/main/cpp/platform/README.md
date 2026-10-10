# 原生平台代码目录

当前仅支持 ARM64。版本运行时实现与版本 ABI 分层归属；公共路径通过源码声明、符号绑定和运行时校准复用，不按厂商复制实现。

| 目录 | 职责 |
| --- | --- |
| `common/` | 跨版本能力、事务、校准、对象与字段迁移、缓存、帧和层级处理 |
| `android13/`、`android17/` | 已有的版本专有运行时实现 |
| `abi/arm64/android11/` … `android17/` | 各版本唯一的固定 ARM64 ABI 声明 |
| `abi/common/` | 不依赖版本或指令集的固定 ART 调用声明 |
| `abi/arm64/` | ARM64 公共布局与汇编；`common/` 保存跨版本 ARM64 ART 声明 |
| `symbols/` | 跨版本符号选择与兼容性解析 |
| `android13/symbols/` | Android 13 符号配置与已核对厂商符号定义 |
| `jni_access/` | JNI 访问能力选择与独立校准入口 |
| `jvmti/` | 公共 JVMTI 后端 |
| `backend_factory.*` | 后端选择入口 |

版本布局只有一个归属，例如 Android 14 的 ARM64 声明在 `abi/arm64/android14/`，不再同时维护 `android14/abi/`。Android 13 原先分散在运行时目录的 `*_abi.h` 也统一归入 `abi/arm64/android13/`。旧的 `arch/arm64/` 汇编也已并入 `abi/arm64/`。

Android 11、12、14、15、16 目前主要通过 `common/` 实现行为，相应 ABI 目录包含实际不同的声明；没有创建空运行时目录或重复复制公共 `.cc` 文件。Android 13 保留其独有的完整后端和测试诊断；Android 17 保留实际不同的 Class 方法表校准。Runtime 服务、接收者、JIT 与结构事务直接复用 `common/`，不保留按版本包装的别名或转发入口。目录存在不表示真机全功能验证完成。Android 12L/API 32 当前不在支持目标中。

新增代码规则：先确认是否确有版本或 ARM64 差异。跨版本算法放 `common/`；版本独有声明放 `abi/arm64/androidXX/`；版本独有行为放相应 `androidXX/`。保留代码命名空间，移动目录不改变运行时语义。厂商差异只有在真实符号/调用语义需要时加入，不能凭机型、SDK 或构建编号授予私有能力。

设备采集库位于工程 assets/runtime_lib 的既有分类目录，不纳入源码提交；原始日志和逐次报告写入已忽略的 `build/evidence/`，验证摘要在 `docs/VERIFICATION.md`。

Android 13 直接使用 `common/field_copy.h`、`common/object_replacement_map.h` 和 `common/retired_*_registry.h` 的公共命名空间；不再为这些公共算法保留版本转发头。`abi/arm64/android13/` 只保留真实的版本声明，不为公共 POD 视图或已有算法重新起版本别名。
