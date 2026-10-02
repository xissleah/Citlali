# Citlali v0.1 宿主协议

以下协议由核心直接使用，因此头文件随 SDK 维护；其具体实现由用户安装的插件提供。业务协议由各插件包维护。所有函数表具有 struct_size/version 前缀，错误与借用规则遵循 abi.h。

## frontend.h：citlali.frontend/service-v1

cli/server 提供 `start`、`request_stop`、`join`。start 启动接收请求的服务；request_stop 幂等并可与 join 并发；join 接受毫秒超时或 UINT64_MAX，在仍运行时返回 TIMEOUT，已终止时允许重复调用。其他调用由调用者串行。destroy 必须停止并等待内部工作结束，禁止后台线程继续执行已卸载代码。具体 CLI 命令/HTTP 路由由插件决定。

## text.h：citlali.inference/text-v1

runtime 提供 create_session、destroy_session、submit、wait、cancel、release_operation、request_stop、plan_summary。宿主退出使用 request_stop；其他方法供 frontend 或消费者使用。request_stop 永久关闭接收新任务，并请求取消已有任务。destroy 必须终止并等待所有工作后释放资源。

基本生命周期：create_session → submit → wait/cancel → release_operation → destroy_session。submit 失败输出空 operation，成功时复制需保留的 prompt，sink/context 借用至 terminal wait。sink 返回非零请求取消。TEXT 字节仅在 callback 内有效；每个任务恰好发送一次 COMPLETED/CANCELLED/FAILED 终态。终态 wait 保证所有回调结束；cancel 仅请求取消，不能据其返回释放上下文。未终止的 operation/session 不得释放。

同一 operation 只允许 cancel 与一个 wait 并发。release/destroy 与其他方法须串行；回调不得 wait/release/destroy/join。UINT64_MAX 表示无限等待，有限超时为 0..86400000 毫秒。TIMEOUT 返回 RUNNING 并保留所有权；失败任务的 terminal wait 成功返回 FAILED，事件携带失败信息。plan_summary 返回借用的不可变 UTF-8，生命周期至实例销毁。具体 prompt、token 限制、历史与采样语义由 runtime 文档定义。

## probe.h：citlali.environment/probe-v1

evaluate(id, op, value, result, error) 同步检查条件，结果为 PASS/FAIL/UNKNOWN。借用字符串不得保留；环境探测发生在 create 前，不得加载模型。缺少 checker 或接口时为 UNKNOWN。op 为 eq/ne/gte/lte/gt/lt；值语义由检查器定义，无效值/操作为 INVALID_ARGUMENT，不能用 UNKNOWN 掩盖错误。FAIL 永远阻止启动；UNKNOWN 默认警告，strict-env 下阻止启动。
