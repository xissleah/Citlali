# gguf-tokenizer 0.1.0

Provides example.gguf/chat-tokenizer-v1, consumes gguf-vocab-v1 and example.gguf/chat-format-v1 from the same bound model instance. create verifies template support. No model-family check or fixed Qwen/Llama formatting is embedded. Protocol headers in include are local copies; Native ABI v1 descriptors identify exact protocol/version.

encode_chat formats one user turn using model chat template then tokenizes using its vocabulary (special tokens/BOS enabled). Prompt replaces history. Token/piece buffers are caller-owned, NULL/0 queries required count; insufficient capacity returns UNSUPPORTED, required set, no writes. identity is borrowed to tokenizer destroy; pieces are raw bytes and must be assembled into UTF-8 by runtime. Input must be UTF-8 without NUL, at most 1 MiB; formatted template at most 4 MiB. Literal special tokens are parsed as model special tokens; this single-user example provides no prompt-injection isolation.

All methods serialized by consumer. Bound model outlives tokenizer. Errors owned by provider are released before unload; no exceptions cross ABI. Table/descriptor remain until library unload, returned resources use provider release methods. Unknown interfaces return UNSUPPORTED with null output.
