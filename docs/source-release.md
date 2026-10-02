# Citlali v0.1 发布范围

发布内容：C++ 核心、SDK、空 list.citlali、用户插件目录及可选 CPU GGUF 示例源码、测试、构建说明、必需 TOML 依赖与许可。产品 v0.1 / 构建 0.1.0，首个公开 ABI 为 citlali.native/v1（数字 1），使用完整接口描述符；功能协议与结构版本独立。

plugins 下的四个包为 gguf-model、gguf-tokenizer、gguf-runtime 和 cli-basic，按功能命名；llama.cpp 是内部后端，来源和 MIT 许可按原名保留。插件包自带 info.toml、protocol.h/md、include、src 与许可证。共享构建、部署和分发说明位于根目录 README.md；模型验收位于 tests/gguf-cpu。没有 examples 目录。

默认不编译 CPU 示例，不需要 llama.cpp，核心清单为空。开启 CITLALI_BUILD_CPU_EXAMPLE 才生成四个包到 dist/<配置>/plugins；用户按 README.md 填写运行目录中的 list.citlali；模型权重、上游完整源码、DLL、CUDA、历史开发插件与实验缓存不在发布源码内。用户使用预编译 CPU 运行包无需另装 llama.cpp 或 CUDA。模型由用户提供。

自有代码采用根目录 LICENSE 中的 MIT License；各插件保留 llama.cpp 原 MIT 版权及声明，运行库适用各自许可。生成运行目录附带项目和第三方许可。没有上传、git push 或联网发布。

2026-10-03 同一套 CPU 插件在 Llama 3.2 1B 和 Qwen3 0.6B 两个 GGUF 模型上验收，切换只修改模型路径。全部 DLL 哈希一致。模型自带聊天模板、词表和停止 token 决定处理方式，代码不分支判断家族。4 项核心测试、2 项模型测试共 6/6 通过；支持边界及提示词 /no_think 的说明见根目录 README.md，记录见 tests/gguf-cpu/validated-models.json。HF Gemma/Qwen3.5 尚未转换或验收。

tests/fixtures/lifecycle 是合成加载 fixture，仅 BUILD_TESTING 开启时编译到 build/testing，不进入默认 dist。生产构建可关闭测试。原个人开发目录保持完整。
