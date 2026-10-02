# Citlali v0.1 包与部署格式

`info.toml` 描述插件信息和条件，`protocol.h` 定义可编译接口，`protocol.md` 说明调用语义。两者必须随包存在，核心不会解析 Markdown 推导接口。动态库实现原生入口，包自带其协议头和运行依赖。

## info.toml

以下是外部 runtime 的清单格式示意，不对应本发行附带的实现；请替换入口路径、身份、条件和所提供接口。

```toml
manifest_version = 1
[plugin]
id = "vendor.my-runtime"
version = "0.1.0"
type = "runtime"
display_name = "My Runtime"
[entry]
path = "bin/my-runtime.dll"
symbol = "citlali_plugin_entry"
abi = "citlali.native/v1"
[protocols]
provides = ["citlali.inference/text-v1"]
[strict_requirements]
os = ["windows"]
arch = ["x86_64"]
[advice]
notes = []
```

entry.path 必须在包内，不能是绝对路径或逃逸包目录。主类型标准名称：model_load、tokenizer、runtime、kernels、cli、server；扩展类型必须采用命名空间形式，例如 vendor.scheduler。标准类型在一次部署中只能启用一个，kernels 除外；不同自定义类型规则由声明的依赖契约决定。统一使用小写标识，禁止 Server/api 等标准别名。

strict_requirements/advice 可以省略或为空；两者没有有效内容时警告。os 支持 windows/linux/macos，arch 支持 x86_64/aarch64。动态条件使用 strict_requirements.probes，当前 checker 使用 self；字段如下：

```toml
[[strict_requirements.probes]]
id = "cuda.compute-capability"
op = "gte"
value = "12.0"
checker = "self"
```

当前动态探测仅支持 checker="self" 且本插件提供 probe-v1；其他 checker 为 UNKNOWN，尚不支持通过依赖槽调用检查器。FAIL 报错，缺少检查能力为 UNKNOWN；默认警告继续，strict-env 报错。无效格式/值或 probe 回调错误始终报错。advice.notes 不作为强制要求。

依赖以 slots 和精确协议表达，不以类型名称推断。插件可声明：

```toml
[[dependencies]]
slot = "kernels"
protocol = "vendor.compute/matmul-v1"
cardinality = "many"
optional = false
```

cardinality 为 one/many；optional 默认 false。slot 没有内置计算含义，自定义 extensions 槽也由插件解释。描述自定义算子不代表 runtime 能自动执行它：runtime 必须消费对应协议，并在准备阶段将实现绑定至执行位置。

## list.citlali

两个根字段必须存在：all_plugins 是本清单登记的可用包，used_plugins 是当前启用和配置的包；首次发布为空数组。核心 scan/enable/disable 管理这些字段，用户按插件文档补充 bindings/config。all_plugins 并非整台机器的全局库存。

下面是用户已安装 runtime 和 CLI 的格式示意；名称为占位符，实际值以包清单为准：

```toml
[[all_plugins]]
id = "vendor.my-runtime"
version = "0.1.0"
path = "./installed/my-runtime"
[[all_plugins]]
id = "vendor.my-cli"
version = "0.1.0"
path = "./installed/my-cli"

[[used_plugins]]
ref = "vendor.my-runtime@0.1.0"
# [used_plugins.config] 在此放 runtime 文档约定的字段
[[used_plugins]]
ref = "vendor.my-cli@0.1.0"
[used_plugins.bindings]
runtime = ["vendor.my-runtime@0.1.0"]
```

上述 CLI 包必须声明 runtime 槽消费 text-v1。相对包路径相对于 list.citlali，而不是当前命令目录。版本格式为精确 major.minor.patch；不支持范围或隐式选择。同一插件 ID 不能同时启用多个版本；绑定目标必须已启用、提供精确协议并满足数量要求；依赖不能形成环。启用插件必须连接到 frontend/依赖图，空清单可以 check。run 要求一个 runtime 以及 cli 或 server。

配置表在 create 时序列化为 UTF-8 TOML 传入实例；其字段和性能策略由插件解释。核心不负责选择某个算子、推断模型图或下载依赖。协议描述符及函数表使用前必须校验，所有资源在对应动态库驻留期间创建和释放。
