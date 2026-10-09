# 原始结构事务唯一 RunState

`native_schema_run_state::RunState` 聚合一个实际原始 `Redefiner::Run` 的类加载暂停观察、source Handle Pool、allocator BatchAccess、完整 JNI/native graph Authority、实际 allocator、私有 DEX publication Ledger、incoming cache Mapper/Owner、连续停线程 Session 和 required-cleanup Facade。生产六个 Provider Run seam 必须使用同一个实例；不会以安装成功或 SDK 数值授予权限。

生产准备者持有 Dependencies 的 process 服务与强全局 cache oracle。CacheCalibration 的 type/field/method 索引来自独立原始 StandardDEX，预热方法实际填充这些缓存；每个 Run 把 cache/Class 放入真实 source Pool，并重新校准该 Run Mapper 的 Owner。allocator 的 FindClass trampoline 可路由到 `RunState::FindOriginal`；普通 bootstrap calibration 则仍由其独立 Runnable context 与同一 calibrated lookup 服务路由。

正常生命周期：

1. 在唯一原始 Run 调用之前，`Begin` 核对实际 runtime/self/VM/driver/Direct gate，分配完整帧扫描存储，创建真实所有权聚合并 Stage JNI/native graph。Session 保留唯一 first-Finish hook，捕获真实 Batch 与 private DexCache；`RefuseActualClass` 通过唯一 original RecordFailure 转发真实 Class signature，不写 driver.result。
2. 唯一实际验证 leaf 的 `BeforeVerification` / `AfterVerification` 成对消费同一 owner/Iterator。`RootedGraph.metadata` 来自已有实际 Holder 根数组的完整分区校验，不能用结构分区数量替代 source 所有定义的数量。
3. 唯一成功 Collected callback 先 `PrepareRoots` 冻结 native slot/container 布局，注册 Plan / Ledger / Authority / guard 的全部强根并 Freeze。这个阶段不 Seal Authority、不授予提交权限。
4. calibrated continuous Session `Begin` 执行真实 pure Thread pair 与 GC/stop reservation。重新 Resolve 已注册强根，刷新 native Plan、Ledger、Authority、缓存。required cleanup 枚举完整 registered threads/physical frames，才允许 hierarchy Seal 和实际 native provider；其新增 bindings 使用 Registry.Existing，禁止追加 JNI 全局根。真实 Facade 只包装该 provider。`ContinueSource` 交给同一原始最终停线程窗口，不跨本地 JNI executor 返回。
5. 原始最终 Update `Finalize` 只刷新已分配位置，不能创建根/容器。SourceCommit 原本验证 Facade，Commit observer 先消费 cleanup witness，再 Hierarchy/DEX Ledger BeginCommit；真实反射、deopt、ReplaceReferences、旧 Class retention、JIT 和 interpreter 清理仍逐项由 Facade 消费。原始 Run 返回后 `End` 只关闭已完成/已退款的生命周期，不做过晚 Holder/DEX 退休。

失败发生在原始可失败边界，`Refund` 必须先释放尚未 Continue 的本地 stop、恢复真实 Runnable，再退休私有临时 Class。Continue 之后的缺失/异常不是可恢复的 JVMTI 拒绝；不得把 original-final-only Window 包装为这种可退款路径。

## 已知边界

这个模块尚需生产接线和实际设备整体回归。当前 Authority/Preservation 接口支持保留原角色/修饰的增量 hierarchy，仍拒接口/Object schema、修饰变化和 receiver 窄化；其拒绝不是完整 schema 已完成。Android 11/12 的独立 pure Thread transition pair 尚未证明，模块保留真实停线程拒绝。Original-final-only 组件证明窗口存在，并没有解决 publication 之后的安全拒绝/退款，所以这里不启用该路径。

`Dependencies.mapping` 提供预分配的映射存储和实际停线程刷新回调。RunState 仅在真实 continuous Session.Begin 成功后刷新，持有一个显式 ReadContext；每次 extent 请求再次核对同一个 Current RunState 与实际 StoppedWindow。Authority、receiver Preservation、incoming cache Mapper 和 interface 闭包使用该上下文，拒绝时不回退 OS；member provider 收到同一个 borrowed 指针。普通 JNI Stage 与独立校准仍使用原有即时映射检查。

这批完成上述读者的显式上下文传播，不能单凭这些测试宣称全部 final consumers 已经零 OS 查询：allocator/source roots、guard 和 provider 的其余调用链需要对应所有者接线与整体回归。组合测试的 SourceOwners 边界替身仍使用普通 Pool.Read 的 Runnable 证明，因而仅针对实际 Mapper.Readable 与 receiver 读取测量零 OS 查询；真实 cache Owner 的正向/拒绝检查仍运行且禁止 native allocation。
