# Citlali v0.1

Citlali 是 C++17 实现的本地推理插件框架。核心负责插件登记、清单检查、协议匹配、依赖装配和生命周期；模型读取、分词、算子、推理调度、CLI 交互及 HTTP 服务由用户安装的插件提供。

这是首个发布版本 **v0.1**，构建版本为 `0.1.0`。源码不附带个人开发用插件、模型权重、GPU 后端或测速实验。裸核心可以管理和检查插件清单，安装并配置 runtime 和 cli/server 插件之后才能执行推理。没有自动下载或插件市场。

需要直接体验推理时，可使用可选的 [CPU 示例套件](#cpu-示例套件)：通过 llama.cpp C API 实现 GGUF 加载、模型聊天模板、文本生成和 CLI，已用 Llama 3.2 1B 和 Qwen3 0.6B 两种架构的 Q4_K_M 模型验证同一套二进制。示例包含 gguf-model、gguf-tokenizer、gguf-runtime 和 cli-basic，默认不编译，模型仍由用户提供；编译后的独立 CPU 包附带所需后端库，用户无需另装 llama.cpp。只提供 CPU 示例，不附带 GPU 实现。

## 从哪里启动

本仓库是**源码发行目录**，根目录没有 `citlali.exe`，`plugins/` 中附带的四个 CPU 插件也是源码，不能直接启动推理。编译后的宿主、插件动态库和运行配置放在构建输出目录。

| 目录或文件 | 用途 |
|---|---|
| 仓库根目录 `plugins/` | CPU 插件源码，以及自行安装的其他插件包 |
| 仓库根目录 `list.citlali` | 空清单模板，不是已配置的推理清单 |
| `build/dist/Release/` | 默认构建输出，只有核心，没有 CPU 推理插件 |
| `build/example/dist/Release/` | 开启 CPU 示例后的运行目录，包含宿主和四个编译好的插件包 |
| 运行目录中的 `list.citlali` | 用户实际登记、选择插件和配置模型的清单 |

**第一次从源码体验推理：**

1. 按[构建 CPU 插件](#构建-cpu-插件)准备匹配的后端并编译；仅执行基础构建不会得到推理能力。
2. 按[配置和运行](#配置和运行)，填写 `build/example/dist/Release/list.citlali`，设置自己的 GGUF 模型路径。
3. 从仓库根目录进入运行目录并启动：

```powershell
cd ./build/example/dist/Release
.\citlali.exe check .\list.citlali --probe
.\citlali.exe run .\list.citlali
```

看到 `> ` 后输入问题，按回车生成；回答结束后可以继续提问，Ctrl+C 退出。每次提问独立，不保留历史。

**已有预编译运行包：**解压完整包，在包含 `citlali.exe` 的目录配置 `list.citlali`，执行上面的最后两条命令即可。当前仓库不提供自动下载运行包的功能。

清单以 `run` 指定的文件为准。源码目录和运行目录中的同名清单不会同步；命令不指定清单时默认使用当前工作目录的 `list.citlali`。运行时建议显式写出路径。

## 构建

需要 CMake >=3.20、C++17 编译器和线程库；Ninja 是下例使用的生成器。当前已验收 Windows x86_64 MinGW UCRT；其他平台的加载代码已实现，但尚未验收。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
& ./build/dist/Release/citlali.exe --version
& ./build/dist/Release/citlali.exe check ./build/dist/Release/list.citlali
```

编译器需在 PATH 内，或显式设置 CMAKE_C_COMPILER/CMAKE_CXX_COMPILER。基础构建离线，不需要 CUDA、llama.cpp 或模型。生产构建添加 `-DBUILD_TESTING=OFF`；启用测试时只有一个隔离的合成加载 fixture 写入 `build/testing`，不会进入 `dist` 或默认清单。Python 3 存在时会额外执行管理命令验收；宿主不依赖 Python。

## 安装与使用插件

把**编译好的插件包**解压到运行目录的 `plugins/<包名>/`。包内必须有 `info.toml`、`protocol.h`、`protocol.md`、清单指向的动态库及所需依赖。源码包应先按作者说明构建，不能直接用于推理。仓库根目录的 `plugins/` 与构建输出中的 `plugins/` 是独立目录，核心构建不会自动复制或编译用户插件。

**复制插件包不会自动更新清单。** 在包含 `citlali.exe` 的运行目录执行下列命令；以插件实际的 `id@version` 替换示例值：

```powershell
.\citlali.exe inspect .\plugins\my-runtime
.\citlali.exe scan .\plugins --deployment .\list.citlali
.\citlali.exe list --deployment .\list.citlali
.\citlali.exe enable vendor.my-runtime@0.1.0 --deployment .\list.citlali
```

`scan` 将直接子目录中的包登记到 `all_plugins`，保留原库存和选择，不下载或自动启用插件。`enable` 只将插件加入 `used_plugins`，不会自动启用依赖或补全绑定和模型配置。按插件说明启用依赖，填写 `used_plugins.bindings` 和 `config`，配置 runtime 和 cli/server 后运行：

```powershell
.\citlali.exe check .\list.citlali
.\citlali.exe check .\list.citlali --probe
.\citlali.exe run .\list.citlali
```

CPU 示例可直接使用下文的完整清单，已包含四个插件的登记、选择和绑定，无需逐个执行 `scan`/`enable`。也可将包安装到其他目录，扫描对应目录并显式指定要更新的清单。

静态 `check` 不加载代码；`--probe` 加载并查询接口/环境，但不创建实例。`run` 完成检查和实例装配，启动 frontend。已知不满足 strict_requirements 时报错；无法判断默认警告后继续，`--strict-env` 将 UNKNOWN 视为错误。advice 只产生提示。默认空清单检查成功，直接 run 会说明缺少 runtime。

## CPU 示例套件

四个可独立安装的原生插件：gguf-model（GGUF 及 CPU 解码）、gguf-tokenizer（模型内聊天模板）、gguf-runtime（异步文本推理）、cli-basic（终端交互）。四个源码包直接位于 plugins/<包名>/，各自携带协议头、文档和许可证；构建和部署说明集中在本 README，测试位于 tests/gguf-cpu。源码是 Citlali 原有适配代码的整理，默认不参与核心构建。仅支持 Windows x86_64 MinGW UCRT 包装；产品版本 0.1.0，native ABI v1。

计算/量化/模型架构支持来自 [llama.cpp](https://github.com/ggml-org/llama.cpp) C API，不启动外部 CLI。示例不含模型、上游完整源码或 GPU 运行库。分词使用 GGUF chat template 和 llama_chat_apply_template 内置模板支持；不是完整 Jinja 引擎，缺少或不支持模板会报错。插件不检查模型家族，也不写死某一家族的聊天格式。用户模型许可证独立于本项目。

### 构建 CPU 插件

以下命令同时构建宿主和四个 CPU 插件；仅基础构建或仅登记源码包不能执行推理。

需要匹配的 llama.cpp 源码、CMake、Ninja、MinGW UCRT（包括 GCC runtime 的 share/licenses/gcc-libs）、Python 3。上游本地快照没有 Git 元数据，不能伪称固定提交；plugins/gguf-model/upstream.json 记录实际使用的公开头 SHA256，脚本拒绝不匹配的头。更换上游版本需同步核验并更新这些记录；头匹配不保证任何来源 DLL 都兼容，必须由同一源码构建。

从 Citlali 仓库根目录执行，路径替换为本机位置：

```powershell
python plugins/gguf-model/tools/build_backend.py --source C:/deps/llama.cpp --output build/llama-cpu-backend --toolchain C:/msys64/ucrt64/bin
cmake -S . -B build/example -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCITLALI_BUILD_CPU_EXAMPLE=ON -DCITLALI_LLAMA_SOURCE=C:/deps/llama.cpp -DCITLALI_LLAMA_CPU_BUILD=C:/path/to/Citlali/build/llama-cpu-backend
cmake --build build/example --config Release
ctest --test-dir build/example -C Release --output-on-failure
```

后端仅编译 CPU、关闭本机探测和可选 x86 ISA，基础包兼容性优先，速度不代表该 CPU 的优化上限。没有 CUDA/Vulkan/Metal、上游 CLI/server/common 库或所有 CPU 变体。CMake 检查后端生成记录及 DLL SHA256，不接受原 GPU 后端冒充 CPU 包。模型设置强制 CPU，禁止 gpu_layers 配置。

构建结果位于 `build/example/dist/Release/`，四个插件包位于其 `plugins/` 子目录。CMake 已将所需后端和运行库复制到包内。分发时复制整个运行目录并保留全部许可证，可自行压缩为 ZIP；本项目目前不提供通用插件一键打包工具。分发自有插件前应自行确认动态库入口、依赖、协议文件、环境要求及许可证完整，并在干净环境验收。

### 配置和运行

取得编译好的 Citlali CPU 运行包即可使用，**无需另外安装 llama.cpp 源码、CLI 或 CUDA**。模型本身由用户提供。

构建生成的 `list.citlali` 默认为空，不自动启用插件，也不再生成单独的示例部署文件。将下面内容替换到 `build/example/dist/Release/list.citlali`（预编译包则是 `citlali.exe` 旁的同名文件），把 `model` 改为自己的 GGUF 路径。不要保留原有 `all_plugins = []` 和 `used_plugins = []` 两行。插件路径相对于清单所在目录；Windows 路径推荐使用 `/`。

```toml
[[all_plugins]]
id = "example.gguf-model"
version = "0.1.0"
path = "./plugins/gguf-model"
[[all_plugins]]
id = "example.gguf-tokenizer"
version = "0.1.0"
path = "./plugins/gguf-tokenizer"
[[all_plugins]]
id = "example.gguf-runtime"
version = "0.1.0"
path = "./plugins/gguf-runtime"
[[all_plugins]]
id = "example.cli-basic"
version = "0.1.0"
path = "./plugins/cli-basic"
[[used_plugins]]
ref = "example.gguf-model@0.1.0"
[used_plugins.config]
model = "C:/models/model.gguf"
[[used_plugins]]
ref = "example.gguf-tokenizer@0.1.0"
[used_plugins.bindings]
model = ["example.gguf-model@0.1.0"]
[[used_plugins]]
ref = "example.gguf-runtime@0.1.0"
[used_plugins.bindings]
model = ["example.gguf-model@0.1.0"]
tokenizer = ["example.gguf-tokenizer@0.1.0"]
[used_plugins.config]
context_size = 2048
threads = 4
[[used_plugins]]
ref = "example.cli-basic@0.1.0"
[used_plugins.bindings]
runtime = ["example.gguf-runtime@0.1.0"]
[used_plugins.config]
max_tokens = 256
```

从源码项目根目录执行：

```powershell
& ./build/example/dist/Release/citlali.exe check ./build/example/dist/Release/list.citlali --probe
& ./build/example/dist/Release/citlali.exe run ./build/example/dist/Release/list.citlali
```

使用预编译运行包时，在运行目录执行 `./citlali.exe run ./list.citlali`。切换模型只需修改 `model`；无需重新构建或更换插件。`CITLALI_EXAMPLE_MODEL` 仅提供可选的验收模型路径，不设置用户运行清单。重新运行 CMake 配置可能把构建输出中的 `list.citlali` 重新生成为源码模板；已配置的清单请提前备份，或另存为 `my-model.citlali` 并用 `run ./my-model.citlali` 启动。

CLI 输入问题并按回车即开始流式生成；回答完成后再次出现 `> ` 提示，可继续提问。按 Ctrl+C 退出，程序会停止当前生成并清理资源；也可用 `:quit` 退出、`:plan` 查看配置。不需要输入等待或取消命令。初版 greedy 采样、单活动请求、每次 prompt 替换历史，context 默认 2048、threads 默认 4、max_tokens 默认 256。模型模板可能启用其思考格式，本套件不提供通用关闭思考开关。CPU 模型内存/速度受设备影响，轻薄本建议先用小型量化 GGUF。

### 输出信息与 Qwen3 思考模式

`prompt_tokens` 统计实际送入模型的完整输入，包括用户文字、聊天模板的角色/结束标记和换行。一个词不一定对应一个 token；短输入显示多个 token 属于正常情况，与保留对话历史无关。`generated_tokens` 表示本次生成的 token 数量。

`/no_think` 是 Qwen3 支持的文本软指令，请求直接回答、不展开思考。可以在问题末尾添加，例如 `你好，请介绍你自己。 /no_think`；普通提问无需添加。它不是 Citlali 命令，其他模型未必支持。插件使用模型自带的聊天模板，不自动附加这条指令。

### 示例许可

Citlali 适配代码：MIT，各插件目录自带 LICENSE。llama.cpp/GGML：MIT，各插件目录自带 llama.cpp-LICENSE，后端未修改。toml++ 与 winpthread 原许可随编译包保留。MinGW libstdc++/libgcc 动态运行库适用 GCC 许可及 Runtime Library Exception，编译后包中的原始说明在 gguf-model/runtime-licenses/gcc-libs；它们不改标为 MIT。不得删除这些版权/许可文件。

### 通用性与验收

插件名称描述 GGUF 能力，与 Llama 模型家族无关；llama.cpp 是内部计算后端，后端符号、依赖配置和许可仍使用其真实名称。同一套 gguf-model、gguf-tokenizer、gguf-runtime、cli-basic 二进制用于不同架构；模型路径和 GGUF 自带的元数据决定架构、词表、聊天模板及停止 token，不需要更换专用插件或重新编译。

2026-10-03，Windows x86_64 CPU 实际验收以下两种模型：

| 模型 | GGUF 架构 | 验收 |
|---|---|---|
| Llama-3.2-1B-Instruct-Q4_K_M.gguf | llama | 数字/英文输出、中文 UTF-8、两次独立请求、Ctrl+C 退出及清理 |
| Qwen3-0.6B-Q4_K_M.gguf | qwen3 | 同样流程，使用同一套插件 DLL |

测试仅替换 model 路径；提示词、线程、上下文、token 上限、绑定和插件二进制保持一致。`/no_think` 是 Qwen3 支持的软指令，用于请求关闭思考、直接回答；它是提示词文本，不是 Citlali 命令，也不是通用模型开关。普通提问无需添加，其他模型可能忽略或误解。验收为了保持提示一致给两种模型都附加该文本，让 Qwen3 更容易在有限 token 预算内给出答案；插件不检测或追加此指令，也不改写模型的默认思考模式。不带该指令的 Qwen3 也已成功生成，但默认思考过程可能耗尽 token 预算。生成测试验证接口/生命周期和有效输出，不保证模型遵循每条指令或每次计算正确。

英文问答与中文输出有效，空闲及生成中的 Ctrl+C 退出、上下文超限、GPU 配置拒绝、无效配置回滚及逆序销毁通过。执行时 PATH 仅保留 Windows/System32；两组验收记录中的全部 DLL SHA256 完全一致。配置上述两个模型时，完整 CTest 包含 4 项核心测试与 2 项模型测试，6/6 通过；未提供测试模型时只执行核心测试。验收摘要见 tests/gguf-cpu/validated-models.json。

配置多模型验收时可传入分号分隔的路径；也可用 `-DCITLALI_EXAMPLE_MODEL=C:/models/model.gguf` 只验收一个模型。模型路径不写入发行清单，测试从本 README 的配置创建临时清单：

```powershell
cmake -S . -B build/example "-DCITLALI_EXAMPLE_TEST_MODELS=C:/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf;C:/models/Qwen3-0.6B-Q4_K_M.gguf"
ctest --test-dir build/example -C Release --output-on-failure
```

通用范围是所选后端支持的 GGUF 架构和内置聊天模板，不能解释为所有模型格式/架构都支持。Gemma/Qwen3.5 HF 权重尚未转换和验收。基础 CPU 构建兼容性优先，性能不代表 CPU 优化上限。已生成的独立运行包包含必要库和完整许可证，模型由用户提供。

## 结构与协议

| 路径 | 用途 |
|---|---|
| src | C++ 宿主与命令入口 |
| plugins | 用户安装的插件包目录，包含 CPU 示例源码和用户插件包 |
| sdk | 基础 ABI、宿主直接消费的协议头、可选 C++ 辅助 |
| tests | 核心 ABI、加载生命周期和管理验收 |
| third_party | TOML 解析依赖与许可证 |
| docs | 清单格式、发布范围及接口说明 |
| list.citlali | 空插件清单模板 |

产品版本与 ABI/功能协议版本独立。首版 v0.1 使用 `citlali.native/v1`（握手数字 1），采用包含协议 ID、版本和函数表的接口描述符。第三方协议通过精确 ID 和 `-vN` 匹配，不自动降级。宿主直接调用的 frontend/text/probe 协议在 SDK 内维护，其余业务协议由发布插件自带。

接口见 [SDK](sdk/README.md)、[包和清单格式](docs/packages.md)、[宿主协议](sdk/protocols.md)。原生插件拥有进程权限，应只加载可信代码。没有崩溃隔离、热卸载或通用 Python/Triton 桥接；具体推理能力和性能取决于安装的插件。

发布范围见 [发布说明](docs/source-release.md)，第三方许可见 [第三方说明](third_party/THIRD_PARTY.md)。项目自有代码采用 [MIT License](LICENSE)，版权署名为 `2026 Citlali contributors`。第三方代码及独立安装的插件遵循各自许可证。
