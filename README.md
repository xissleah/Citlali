# Citlali

**按模型、设备和使用场景，组合自己的本地推理引擎。**

Citlali 是一个以插件为基础的本地推理框架，核心由 C++17 实现。模型加载、分词、算子、执行调度和应用接口由插件提供，用户通过部署清单选择、连接和配置所需组件。

插件可以为特定模型和硬件做专门优化，也可以封装现有推理后端。模型架构、计算设备和性能策略由插件决定；核心负责插件检查、协议匹配、依赖装配和生命周期管理。

## 自定义与扩展

| 组件 | 可定制的内容 |
|---|---|
| `model_load` | 权重格式、加载方式、量化数据与模型资源 |
| `tokenizer` | 分词、输入预处理、聊天模板 |
| `kernels` | 算子实现、融合计算、特定设备优化 |
| `runtime` | 模型执行、调度、缓存、采样和推理策略 |
| `cli` / `server` | 终端交互、服务接口和请求处理 |
| 自定义类型 | 使用 `vendor.name` 命名，扩展视觉编码器、调度组件等能力 |

**用协议连接插件。** 每个插件声明它提供和依赖的接口，部署清单显式指定绑定。标准类型统一命名；一次部署允许多个 `kernels`，其他标准类型各启用一个。自定义类型通过依赖契约组织。

**让优化留在插件内。** 插件作者可以选择支持的模型、设备和运行环境，自行实现专用算子、融合或图执行。依赖解析和接口查询可以在初始化阶段完成，计算阶段直接调用已绑定的函数表。实际性能取决于插件实现和组合方式。

**扩展模型架构和输入形式。** MoE 的专家路由与执行可以由 runtime 和计算插件实现；多模态插件可以定义图片、音频等数据接口，并由相应 frontend 调用。新的业务协议由插件包发布，消费者须实现对应协议。现有 `text-v1` 提供文本推理接口，多模态数据契约需由插件另行定义。

**自由选择实现技术。** 原生插件通过 C ABI 接入，内部可使用 C++、CUDA 或其他工具。使用 Python、Triton 等技术时，插件作者需提供满足 ABI 的适配层及运行依赖。

## 构建核心

需要 CMake ≥ 3.20、C++17 编译器和线程库。下面使用 Ninja，命令从仓库根目录执行：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

默认构建生成核心和空部署清单，输出位于 `build/dist/Release/`。编译器需在 PATH 中，或通过 `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` 指定。生产构建可添加 `-DBUILD_TESTING=OFF`；Python 3 用于部分测试，核心运行不依赖 Python。

本仓库分发源码，运行前需要编译。插件须匹配宿主平台及二进制接口；已验证的构建环境为 Windows x86_64 MinGW UCRT。

## 组合与运行

将编译好的插件包放入运行目录的 `plugins/<包名>/`，按插件文档登记、启用并配置。以下命令在包含 `citlali.exe` 的目录执行，插件 ID 替换为实际值：

```powershell
.\citlali.exe scan .\plugins --deployment .\list.citlali
.\citlali.exe list --deployment .\list.citlali
.\citlali.exe enable vendor.my-runtime@0.1.0 --deployment .\list.citlali
```

`list.citlali` 包含两部分：

- `all_plugins`：此部署登记的插件及其路径。
- `used_plugins`：启用的插件、依赖绑定和配置。

复制包后执行 `scan` 才会更新库存；`enable` 只加入选择。按插件说明配置依赖和参数，启用一个 runtime 及 cli/server，然后启动：

```powershell
.\citlali.exe check .\list.citlali --probe
.\citlali.exe run .\list.citlali
```

插件路径相对于清单所在目录。源码目录与运行目录中的清单独立，运行时使用命令指定的文件。包信息可通过 `inspect <包路径>` 查看，选择可通过 `disable <id@version>` 移除。

静态 `check` 检查清单，`--probe` 进一步加载和查询接口及环境。已知不满足 `strict_requirements` 时阻止启动；无法判定时默认警告继续，`--strict-env` 可将其视为错误。`advice` 提供建议。

## 开发插件

插件包携带以下内容：

| 文件 | 职责 |
|---|---|
| `info.toml` | 身份、类型、入口、协议、依赖与环境条件 |
| `protocol.h` | 可编译的接口定义及必要头文件 |
| `protocol.md` | 数据格式、调用语义、所有权和并发约定 |
| 动态库及依赖 | 原生入口、实现和运行所需资源 |

实现 `citlali.native/v1` 入口及实例的创建、销毁方法，声明提供的版本化协议，并通过宿主接口获取显式绑定的依赖。共享函数表与资源需遵守 ABI 的所有权和生命周期约定。自定义算子须由 runtime 消费相应协议并绑定到执行位置。

接口与格式详见 [SDK](sdk/README.md)、[插件包与部署清单](docs/packages.md)、[宿主协议](sdk/protocols.md)。插件在宿主进程内执行，应加载可信代码，并在分发时携带必要依赖、协议和许可证。

## 示例：GGUF 文本推理

仓库提供一组可选的 CPU 插件，展示从模型加载到终端生成的完整组合：`gguf-model`、`gguf-tokenizer`、`gguf-runtime` 和 `cli-basic`。计算后端使用 [llama.cpp](https://github.com/ggml-org/llama.cpp) C API，默认构建不开启此套件。

该示例支持后端可处理的 GGUF 模型与内置聊天模板，采用 greedy 采样、单活动请求和独立提问。需要用户提供模型；示例构建环境为 Windows x86_64 MinGW UCRT。

<details>
<summary>构建示例插件</summary>

准备 CMake、Ninja、Python 3、MinGW UCRT（含 `share/licenses/gcc-libs`）以及与 [upstream.json](plugins/gguf-model/upstream.json) 头文件哈希匹配的 llama.cpp 源码。从仓库根目录执行，替换本机路径：

```powershell
python plugins/gguf-model/tools/build_backend.py --source C:/deps/llama.cpp --output build/llama-cpu-backend --toolchain C:/msys64/ucrt64/bin
cmake -S . -B build/example -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCITLALI_BUILD_CPU_EXAMPLE=ON -DCITLALI_LLAMA_SOURCE=C:/deps/llama.cpp -DCITLALI_LLAMA_CPU_BUILD=C:/path/to/Citlali/build/llama-cpu-backend
cmake --build build/example --config Release
```

宿主和四个插件包生成到 `build/example/dist/Release/`。后端脚本采用基础 CPU 指令集配置；CMake 校验构建记录和 DLL 哈希，并复制必要库。使用完整预编译运行包时无需另装 llama.cpp。

</details>

<details>
<summary>配置模型并启动 CLI</summary>

将下面内容写入 `build/example/dist/Release/my-model.citlali`，把 `model` 改为本机 GGUF 路径。这份清单包含四个插件的登记与绑定，可以直接使用。

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

从仓库根目录执行：

```powershell
cd ./build/example/dist/Release
.\citlali.exe check .\my-model.citlali --probe
.\citlali.exe run .\my-model.citlali
```

出现 `> ` 后输入问题，按回车流式生成；回答完成后继续提问，Ctrl+C 退出。每次提问独立，可用 `:plan` 查看配置、`:quit` 退出。线程、上下文和输出上限在清单中配置。

`prompt_tokens` 包含用户输入及聊天模板标记。Qwen3 可在问题末尾添加 `/no_think` 请求直接回答，该指令由模型理解。

也可编辑运行目录的 `list.citlali`，但重新配置 CMake 时它可能恢复为空模板；独立命名的清单便于保留本机配置。分发时保留完整运行目录和所有许可证，模型按其许可单独提供。

</details>

## 项目结构

| 路径 | 内容 |
|---|---|
| `src/` | 核心与命令入口 |
| `sdk/` | 基础 ABI、宿主协议和开发辅助 |
| `plugins/` | 插件源码包 |
| `docs/` | 格式和接口说明 |
| `tests/` | ABI、生命周期及功能测试 |
| `third_party/` | 核心依赖与许可 |
| `list.citlali` | 空部署清单模板 |

## 许可证

Citlali 自有代码采用 [MIT License](LICENSE)。插件和后端依赖遵循各自许可证；GGUF 示例保留 llama.cpp 的 MIT 声明及所分发运行库的原始许可。详见[第三方说明](third_party/THIRD_PARTY.md)。
