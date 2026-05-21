# HTTP Content Encoding 支持计划

**目标：** 支持响应实体的 `gzip`、`deflate`、`br`、`identity` 解码，并补齐单测。

**范围：** HTTP 解析层新增 content coding 解码模块；请求/响应流程保持 WSK/TLS 主路径不变。

## 任务

- [x] 新增 content coding 类型、解码入口和输出视图。
- [x] `identity` 零拷贝返回原始实体。
- [x] `gzip` 解析 gzip 包装头和 trailer，使用 DEFLATE 解码压缩体。
- [x] `deflate` 支持 zlib 包装和 raw DEFLATE。
- [x] `br` 引入可内核编译的 Brotli 解码源码，并通过显式 allocator 接入。
- [x] `ParseResponse` 在 transfer coding 后按 `Content-Encoding` 应用实体解码。
- [x] 补充 gzip、deflate、br、identity、未知编码、缓冲区不足等测试。
- [x] 运行 HTTP parser 单测，必要时运行主工程构建。
