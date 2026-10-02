# Citlali SDK v0.1

产品发行 v0.1 对应构建版本 0.1.0。首次公开发行的握手常量为 `CITLALI_NATIVE_ABI=1`，清单必须声明 `citlali.native/v1`。ABI v1 采用当前完整的接口描述符布局；内部开发期间的 ABI 编号不作为公开版本延续。公共结构 V1 后缀表示结构代次。功能协议的版本独立演进。

`abi.h` 是基础 ABI 的维护源。接口使用标准 C 类型、显式字节/元素数量、固定宽度整数、cdecl、opaque handle。只保证匹配平台/工具链 ABI 的插件能够互操作；不承诺跨 OS/架构二进制兼容。C++、CUDA、其他语言均可实现原生入口；需要满足相同二进制契约，核心不内置它们的运行时。

包内放 `info.toml`、`protocol.h`、`protocol.md`、必要头文件和原生动态库。`protocol.h` 引用包内携带的基础 ABI 与业务协议头，用户编译消费者不应依赖作者仓库路径。SDK 的 `frontend.h`、`text.h`、`probe.h` 是宿主直接调用的协议维护源；自定义算子、模型加载、分词及调度协议由插件作者定义并随包发布。`plugin_support.h` 是可选的私有 C++ 辅助，不属于公开协议，不要求其他语言使用。

入口签名见 `CitlaliEntryV1`；常规符号名 `citlali_plugin_entry`，通过 `CITLALI_EXPORT` 导出 C 符号。入口与 query 不得初始化模型资源；实际资源在 create_instance 时分配。返回 API 身份、版本、类型、协议集必须与 info.toml 一致。创建失败必须把 out handle 置空并清理已分配资源。

`query_interface` 与 `query_dependency` 返回不可变插件所有的 `CitlaliInterfaceV1` 描述符，其中包括结构大小、精确 protocol_id、protocol_version 和函数表指针。函数表以 `CitlaliTableV1` 开始。核心检查描述符/前缀、精确 ID、正整数版本与 `-vN` 一致性；具体消费者还要检查完整表大小和必要回调。没有自动跨版本替换。表和描述符直到动态库卸载才失效。

初始化错误槽为零并设置 struct_size。复制错误信息后调用 release(owner)，且必须早于卸载代码。静态信息可无 owner/release，禁止栈字符串。错误文本、输入 UTF-8 字节视图都是借用；NULL 只允许长度为零。谁分配对象，谁提供销毁/释放方法；禁止释放外来分配。异常不得跨越 ABI、线程入口和 callback 边界。UNSUPPORTED 查询必须输出空指针。

Host context 提供线程安全日志和显式绑定依赖的数量、实例、接口查询，以及包/部署目录视图。依赖按拓扑顺序创建，按逆序销毁。消费者不能销毁提供者。传入 create 的 TOML 字符串只在调用期间有效；保留时要复制。Host context 结构本身保留时也要复制。配置解析与协议发现应在准备阶段完成，稳态计算可直接使用准备好的函数指针与资源。

退出先停止 frontend 接收请求，再通知 runtime 停止，等待工作线程和回调结束，最后释放资源与动态库。清理失败时宿主非零退出，不强制卸载仍在工作的代码。没有原生崩溃隔离或热重载。宿主直接消费的协议语义见 [protocols.md](protocols.md)，包格式见 [packages.md](../docs/packages.md)。
