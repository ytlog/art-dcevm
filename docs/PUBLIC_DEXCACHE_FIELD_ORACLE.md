# DexCache 字段的公开只读校准入口

`JNI GetFieldID(cacheClass, "resolvedFields", "J")` 是名字查找路径。固定 AOSP 版本的 `FindFieldJNI` 执行 hidden API 策略；拒绝时可表现为 `NoSuchFieldError`，不能把该异常解释为字段不存在，也不能清空异常继续读另一个私有布局。

另一条公开路径是 JVMTI 枚举出的原始 `jfieldID`：

| 固定源码 | `GetClassFields` 实现 |
| --- | --- |
| Android 11–15 | 枚举完整 `GetSFields()` / `GetIFields()`，每个字段通过 `EncodeArtField` 返回 |
| Android 16–17 | 枚举完整 `GetFields()` / `NumFields()`，每个字段通过 `EncodeArtField` 返回 |

这些实现不依据 public/private 或 hidden API 名单过滤。`GetFieldName`、`GetFieldModifiers`、`GetFieldDeclaringClass` 从原始 ID 解码读取字段身份。JNI `ToReflectedField` / `FromReflectedField` 执行原始字段反射转换；`GetLongField` 的 primitive 宏直接解码已有 ID、读取实际对象字段，不再调用按名字的 `FindFieldJNI`。CheckJNI 仍验证接收者、字段类型和 static 模式。

事实来自每个固定 tag 的原始源码，例如 [Android 11 GetClassFields](https://android.googlesource.com/platform/art/+/refs/tags/android-11.0.0_r1/openjdkjvmti/ti_class.cc#549)、[Android 16 GetClassFields](https://android.googlesource.com/platform/art/+/refs/tags/android-16.0.0_r1/openjdkjvmti/ti_class.cc#546)、[Android 17 GetClassFields](https://android.googlesource.com/platform/art/+/refs/tags/android-17.0.0_r1/openjdkjvmti/ti_class.cc#545)、[Android 17 field metadata](https://android.googlesource.com/platform/art/+/refs/tags/android-17.0.0_r1/openjdkjvmti/ti_field.cc)、[Android 17 JNI field access](https://android.googlesource.com/platform/art/+/refs/tags/android-17.0.0_r1/runtime/jni/jni_internal.cc)。全部 Android 11–17 的下载 SHA-256、URL 和逐函数审计留在忽略的 `build/evidence/public-field-source-audit/`。

`native_cache_public_fields::Service` 实现独立只读观察：确切 bootstrap DexCache Class、实例自身 Class、JVMTI declared instance `J` 字段、原 ID 的反射 roundtrip、真实 Runnable 回调中的命名 `Field::GetArtField` 与原 `JniIdManager::DecodeFieldId` 一致、已独立校准元数据中的原生 name/type/declaring Class 一致，最后用原 JNI `GetLongField` 读取。Witness 持有 cache、declaring Class 和 reflected Field 的真实强引用；失败时不替换旧输出，每个 JNI/JVMTI 叶后的异常都停止并保留，错误返回的非空 JVMTI buffer 同样释放。

该 witness 没有存储范围、cache 布局或写入权限，也没有独立功能可用性标志。主生产路径当前不调用它。源码证明的是这条 ID 路径的实现语义；Host 测试验证的是所有者、错误和身份条件，不能证明厂商真机已经允许访问。

下一步在新 APK 的普通 Native JNI 阶段，对真实 Java writer 已建立的同一 DexCache 执行观察并记录原 field ID/declaring identity/value。分别证明公共值与唯一原生 cache 候选相符、对象移动后的 Class/Field 身份、原 writer 对目标缓存槽的实际发布、重新借用和最终窗口一致性。只有这些实际设备证据满足后，才可由原 cache 校准服务消费这份只读输入；不关闭 hidden API 策略，不把字段名或一个 nonzero long 值直接当作 capability。
