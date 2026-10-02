# Citlali v0.1 第三方依赖

- `third_party/toml.hpp`：toml++ 3.4.0，MIT；许可见 `third_party/tomlplusplus-LICENSE`，用于解析 TOML。
- Windows MinGW 构建将 libgcc/libstdc++ 静态链接，winpthread 动态库复制到可执行文件旁；许可见 `third_party/mingw-winpthreads-LICENSE`。再分发工具链运行库时遵守其原许可。

默认核心发行不携带 CUDA、模型或编译好的推理插件。源码提供可选 CPU 示例适配代码，启用构建时才生成推理插件。用户安装的插件负责声明和附带自己的依赖与许可。上述许可证不等于 Citlali 自有代码许可证；Citlali 自有代码采用根目录 LICENSE 中的 MIT License；第三方代码继续遵循其原许可证和版权声明。

可选 plugins 为 Citlali 自有适配源码，使用外部 llama.cpp/GGML 的原生库接口（MIT）。上游来源 https://github.com/ggml-org/llama.cpp，原版权与许可见 plugins/gguf-model/llama.cpp-LICENSE；构建时一并装入每个示例插件包。上游源码未修改，也未整库复制进本仓库。实际头文件快照记录在 upstream.json；本地来源没有 Git 元数据，未声称某个不可核实的提交。

CPU 后端的 MinGW libstdc++/libgcc 使用动态运行库，GCC 许可及 Runtime Library Exception 的原文由构建脚本复制到 gguf-model/runtime-licenses/gcc-libs。它们不作为 MIT 代码再许可。模型权重不随源码或运行包分发，用户须遵循模型自身许可。
