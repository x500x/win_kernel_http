# 协议完整性整改设计

## 背景

当前实现已经覆盖 HTTP/1.1、HTTP/2、WebSocket、TLS 1.2/1.3、证书校验和 WSK 传输的主要路径，并且已有用户态回归测试。但从协议完整性看，它更接近“内核可用的现代客户端子集”，不能直接宣称 HTTP/WebSocket/TLS 底层协议完整无误。

本设计用于把能力边界讲清楚，并补齐会影响互通性、安全性或协议语义的缺口。实现仍保持 Windows kernel 路线：传输层优先 WSK，密码学优先内核 CNG/BCrypt，不引入 WinHTTP、WinINet、SChannel 作为内核主路径。

## 目标

- 对已经声明支持的能力做到协议语义完整，而不是靠宽松解析或重试掩盖错误。
- WebSocket 支持服务端分片消息接收、控制帧穿插、子协议响应校验和消息级错误处理。
- TLS 1.2 / TLS 1.3 版本选择必须基于可验证协商结果；TLS1.2-only 场景必须先确认是真正的 TLS1.2-only，再选择 TLS1.2 路径。
- HTTP/2 不返回未收到 `END_STREAM` 的半截响应，并对协议错误发送合适的 `RST_STREAM` 或 `GOAWAY`。
- 证书验证能力和限制可审计，逐步补齐 IP SAN、撤销策略等缺口。
- 文档和测试同步更新，README/API 文档不得把未实现的 optional 能力写成已完整支持。

## 非目标

- 不把 WinHTTP、WinINet、SChannel 作为内核实现路线。
- 不用“兜底设计”作为正式架构手段。
- 不把任意 TLS 失败都改成 TLS1.2 重连；只有确认版本协商结果后才允许选择 TLS1.2。
- 不一次性实现所有 RFC optional 扩展，例如 WebSocket permessage-deflate、HTTP/2 server push、TLS 客户端证书、所有历史 TLS cipher suite。
- 不支持在高于 `PASSIVE_LEVEL` 的上下文执行同步网络、TLS、证书验证或回调。

## 当前风险分层

### WebSocket

Frame 层已有基础校验：RSV 位、未知 opcode、服务端 mask、控制帧 FIN 和长度、最短扩展长度编码等都已覆盖。主要缺口在消息层：

- `WebSocketClient::ReceiveMessage` 当前拒绝 `FIN=0` 和 `Continuation`，因此不能接收服务端分片消息。
- 高层 `KhWebSocketReceiveSyncImpl` 总是把返回消息标记为最终分片，无法表达聚合前后的真实状态。
- 请求支持发送 `Sec-WebSocket-Protocol`，但握手校验没有确认服务端返回的子协议是否属于客户端请求集合。
- 未做文本消息 UTF-8 校验；这会影响 RFC 6455 文本帧语义和关闭码选择。
- 未支持 WebSocket 扩展协商。该项可以保留为非目标，但必须在能力说明里写清楚。

### HTTP/1.1

HTTP/1.1 parser 支持 `Content-Length`、响应 `Transfer-Encoding` 链（`chunked`/`gzip`/`deflate`/`compress`）、close-delimited、HEAD/101/无 body 状态码，高层也会跳过非 101 的 1xx 中间响应。主要边界是：

- 请求体只支持 `Content-Length`，没有 chunked 上传。
- trailer 只跳过，不暴露给调用者。
- 未知 transfer coding 明确返回 `STATUS_NOT_SUPPORTED`，`br` 只作为 `Content-Encoding` 支持。
- 响应读取以 workspace 聚合为主，不是任意大响应的真正流式解析器。

这些限制可以作为当前支持范围，但文档必须避免写成完整 HTTP/1.1 通用栈。

### HTTP/2

HTTP/2 已支持 preface、SETTINGS/ACK、HEADERS/CONTINUATION、DATA、PING、GOAWAY、WINDOW_UPDATE、HPACK 和 h2c upgrade。主要风险是：

- `ReceiveResponseFrames` 在收到超时且已有 headers/body 时可能提前成功返回半截 body；HTTP/2 必须以 `END_STREAM` 作为响应结束信号。
- 多流生命周期简化，非目标 stream 大多忽略，没有完整并发流调度。
- 协议错误多数只返回本地错误，没有按 HTTP/2 语义发送 `RST_STREAM` 或 `GOAWAY`。
- push、priority、复杂 flow-control 和 trailer handling 是有限子集。

### TLS

TLS 1.2 支持 ECDHE_RSA/ECDHE_ECDSA + AES-GCM；TLS 1.3 支持 AES-GCM、HRR、ticket/PSK/early data 的部分路径。主要风险是：

- 当允许 TLS1.2-TLS1.3 时，当前 `TlsConnection::Connect` 优先跑 TLS1.3，失败后不会形成严格的版本协商分类。
- TLS1.2-only 支持不能靠“TLS1.3 失败后试 TLS1.2”判断；必须确认失败原因确实是版本协商结果。
- TLS1.3 `CertificateRequest` 直接返回不支持；TLS1.2 客户端证书也只发送空证书。
- 不支持 ChaCha20-Poly1305、CBC 等 cipher suite。CBC 可以保持非目标，ChaCha20 是否支持应由性能/兼容性再评估。

### 证书验证

证书验证已有 DER 解析、有效期、SAN/CN 主机名、EKU、KeyUsage、BasicConstraints、链签名、信任锚和 SPKI pin。主要缺口是：

- SAN 只解析 dNSName，没有 iPAddress SAN。
- 没有 IDNA 处理。
- 没有 OCSP/CRL 撤销检查。
- 签名算法和公钥算法是现代子集，不覆盖所有 RFC 5280 历史算法。

## TLS1.2-only 确认设计

TLS 版本选择必须区分“服务器真实只支持 TLS1.2”和“网络错误、证书错误、ALPN 错误、恶意降级或实现 bug”。设计上把它拆成版本协商分类，而不是无条件重试。

### 可进入 TLS1.2-only 确认的条件

只有同时满足以下条件才进入确认流程：

- 请求配置允许 TLS1.2 和 TLS1.3，即 `MinimumProtocol <= TLS1.2` 且 `MaximumProtocol >= TLS1.3`。
- TLS1.3 路径失败发生在版本协商早期，且错误来源可以归类为版本不匹配、服务器选择 TLS1.2 ServerHello、`protocol_version` alert 或无 TLS1.3 `supported_versions`。
- 失败不是证书校验、ALPN mismatch、TCP/WSK timeout、内存不足、用户取消、TLS1.3 CertificateRequest 不支持、record 解密失败或 Finished 验证失败。
- 后续确认使用同一个 origin 配置：host、port、SNI、ALPN 列表、证书策略、pin 和 trust store 都不能变化。

### 确认证据

确认流程需要至少得到以下证据：

- 能解析到合法 TLS1.2 ServerHello，且 cipher suite 属于当前 TLS1.2 支持集合。
- TLS1.2 握手完整通过，包括证书链校验、ServerKeyExchange/CertificateVerify 或 Finished 验证。
- 如果客户端曾提供 TLS1.3，TLS1.2 ServerHello.random 不得包含 TLS1.3 downgrade sentinel；若包含，必须失败并报告协议错误。
- ALPN 结果必须符合调用方请求，不能把 ALPN mismatch 当成 TLS1.2-only。
- 证书校验失败时返回原始信任错误，不能因为 TLS1.2 探测成功而改写为版本兼容结论。

### 状态与缓存

可以增加一个小型 origin 能力缓存，但缓存值必须带来源和 TTL：

- `Tls13Capable`
- `ConfirmedTls12Only`
- `Unknown`
- `RejectedDowngrade`

缓存 key 包含 host、port、SNI、ALPN 集合、最小/最大 TLS 版本、证书策略和 trust store 标识。`ConfirmedTls12Only` 只允许缩短下一次连接的版本选择，不允许绕过证书、ALPN 或 pin 校验。

## WebSocket 接收设计

接收路径新增消息聚合状态：

- 当前消息 opcode：Text 或 Binary。
- 当前聚合长度，受 `MaxMessageBytes` 限制。
- 是否处于 fragment sequence。
- 文本消息 UTF-8 增量校验状态。

接收循环继续逐帧读取。遇到 Ping 时立即发送 Pong；遇到 Pong 可忽略或交给未来 callback；遇到 Close 时返回 close 消息并更新连接状态。数据帧规则如下：

- Text/Binary 且 `FIN=1`：单帧消息，直接返回。
- Text/Binary 且 `FIN=0`：开始分片聚合。
- Continuation 且已打开分片：追加 payload；`FIN=1` 时完成消息。
- Continuation 但没有打开分片，或分片中又收到新的 Text/Binary：返回协议错误并发送 close。
- 聚合超过 `MaxMessageBytes`：返回 `STATUS_BUFFER_TOO_SMALL`，并按 WebSocket close 1009 发送关闭帧。

默认 API 返回完整消息；如果未来需要 fragment callback，应作为显式 receive option，不改变默认聚合语义。

## HTTP/2 错误处理设计

HTTP/2 响应读取必须以 `END_STREAM` 或 `RST_STREAM`/`GOAWAY` 为终态：

- 收到 timeout 但 stream 未结束时返回 `STATUS_IO_TIMEOUT`，不能把已有 body 当成成功响应。
- 目标 stream 上 malformed frame、非法 CONTINUATION、DATA before response headers 等发送 `RST_STREAM(PROTOCOL_ERROR)`。
- 连接级错误发送 `GOAWAY(PROTOCOL_ERROR)`。
- 收到 `PUSH_PROMISE` 且本端设置 `ENABLE_PUSH=0` 时发送 `GOAWAY(PROTOCOL_ERROR)`。
- 非目标 stream 的帧按规范处理：未知 frame 忽略；可识别但非法的 frame 发送对应错误，而不是全部静默丢弃。

## 证书验证设计

短期先补齐不会引入网络依赖的验证：

- 支持 iPAddress SAN，与 URL host 的 IP literal 匹配。
- 明确 dNSName/CN 只处理 ASCII host；IDNA 暂列非目标或后续任务。
- 增加 `RequireRevocationCheck` 策略时，如果没有 OCSP/CRL 实现则返回 `STATUS_NOT_SUPPORTED`，不能静默当作通过。

OCSP/CRL 会引入额外 HTTP 请求、缓存、时间策略和内核网络重入问题，应单独设计，不混入本轮 P0/P1 修复。

## 测试策略

- WebSocket：新增服务端分片接收、控制帧穿插、非法 continuation、子协议 mismatch、文本 UTF-8 错误测试。
- TLS：新增版本协商分类测试，覆盖 TLS1.3 成功、合法 TLS1.2 选择、downgrade sentinel、证书/ALPN/timeout 不进入 TLS1.2-only 分类。
- HTTP/2：新增未收到 `END_STREAM` 超时不得成功、非法 CONTINUATION、PUSH_PROMISE、RST_STREAM/GOAWAY 发送测试。
- 证书：新增 IP SAN、dNSName 优先于 CN、revocation required 返回不支持测试。
- 最终运行所有用户态协议测试，并执行 Debug x64 构建；不运行项目禁止的 `tests/integration/https_smoke.ps1` 命令。

## 文档策略

README 和 API 文档使用“支持的协议子集”和“已实现能力”描述，不使用“完整 HTTP/2 / 完整 TLS / 完整 WebSocket”这类过度承诺。对明确非目标的 optional 能力单独列出，避免调用方误用。
