# HTTP/WebSocket 协议完整性复核说明

## 结论

截至 2026-06-09，本项目不能被描述为“HTTP/WebSocket/TLS/HTTP2 全 RFC optional 能力完整无误”的通用协议栈；它更准确的定位是“面向 Windows kernel driver 的现代客户端协议子集”。在这个已声明的子集内，HTTP/1.1 framing、redirect/retry 安全、WebSocket 握手与消息级接收、TLS 1.3 PSK/HRR/0-RTT 约束、证书校验硬边界、WSK socket 所有权和 PASSIVE_LEVEL 约束都有对应实现和测试线索。

本轮复核后的当前口径是：已支持子集内的互通/严格性缺口已经补齐到测试覆盖范围，包括 HTTP HTTPS 路径的 TLS1.2 confirmation、HTTP/2 `TE`/`content-length`/flow-control/1xx 严格性、HTTP/1.1 空 header 值/`Content-Length` 等值列表/transfer-coding 参数/chunk extension、WebSocket public control-frame API 与 selected subprotocol 查询。仍保留为延期或非目标的协议宽度包括 HTTP proxy/CONNECT、WebSocket frame metadata/分片回调、WebSocket extension/permessage-deflate、TLS client certificate、OCSP/CRL、完整 Name Constraints、IDNA、更多 cipher suite、HTTP/2 完整多流调度和优先级等。这些不应作为隐式能力宣传；若产品目标要求“更完整协议栈”，需要另行按计划文档逐项补齐。

## 复核口径

- “已支持子集完整”：已经公开承诺的能力需要严格解析、严格错误处理、有测试覆盖，不通过宽松解析或任意重试掩盖协议错误。
- “全量协议完整”：覆盖 RFC optional/extension/部署常见能力，包括代理、扩展、撤销检查、客户端证书、更多 HTTP/2 行为等。
- 本项目当前采用第一种口径。README 和 API 文档已经明确使用“协议子集”和“不支持能力”措辞。

## 本地代码证据

### HTTP/1.1

关键文件：

- `src/KernelHttpLib/http/HttpParser.cpp`
- `src/KernelHttpLib/http/HttpRequest.cpp`
- `src/KernelHttpLib/http/HttpTransferCoding.cpp`
- `src/KernelHttpLib/engine/HttpEngine.cpp`
- `tests/http_parser_tests.cpp`
- `tests/khttp_tests.cpp`

已确认的支持能力：

- 请求构造拒绝用户设置 `Transfer-Encoding`，请求体默认走 `Content-Length`，也支持显式 chunked 请求体。
- 响应解析覆盖 `Content-Length`、chunked、transfer-coding 链、close-delimited、HEAD/101/无 body 状态码。
- `Transfer-Encoding` 与 `Content-Length` 共存、重复 chunked、非法 `identity`、非法 trailer、禁止 trailer 字段等有负向测试。
- 自动 redirect 支持 RFC 3986 相对引用，并清理跨源敏感头；HTTPS 到 HTTP 降级默认拒绝。
- stale/reused connection 自动 fresh retry 只允许安全/幂等请求，避免重放非幂等请求。
- close-delimited 和 101 upgrade 响应不会进入普通 HTTP 连接池。

当前边界：

- request chunked upload 需要调用方显式选择 body mode；request trailer 暂不支持。
- response trailer 会校验、消费并通过只读 API 暴露。
- `BodyCallback` 是聚合/解码后一次回调，不是网络流式分块接口。
- 不支持 HTTP proxy/CONNECT/TRACE。
- 请求 header 空值、`Content-Length: 5, 5` 这类等值列表、transfer-coding 参数、chunk extension 已按支持子集补测试。

### WebSocket

关键文件：

- `src/KernelHttpLib/websocket/WebSocketFrame.cpp`
- `src/KernelHttpLib/client/WebSocketClient.cpp`
- `src/KernelHttpLib/engine/WsEngine.cpp`
- `tests/websocket_frame_tests.cpp`
- `tests/websocket_client_tests.cpp`

已确认的支持能力：

- 握手校验 `Sec-WebSocket-Accept`，拒绝重复 accept、HTTP/1.0 101、未请求扩展和不匹配 subprotocol。
- client frame 编码按 RFC 6455 mask，做 payload length 与 required size 溢出检查。
- server frame 解码拒绝 masked server frame、RSV、未知 opcode、非法控制帧、非最短长度。
- 接收路径支持分片 text/binary 聚合，允许 Ping 穿插并自动 Pong。
- 接收超大消息按 1009 关闭；协议错误按 1002 关闭；文本消息做 UTF-8 校验。
- Pong 可以被上层观测；空 text/binary/continuation 有测试覆盖。

当前边界：

- 不支持 WebSocket extensions，例如 permessage-deflate。
- 默认 API 返回完整消息，不暴露 curl 风格逐 frame metadata/分片回调。
- 未实现 partial send/receive 的完整公开 API 契约。
- 公开 `SendPing`、`SendPong`、`Close(code, reason)`；`AutoReplyPing=false` 时调用方能看到 Ping 并显式发送 Pong。
- 主动 close 有 `CloseEx` 路径表达 close code/reason，底层关闭仍按有界等待/关闭传输处理。
- selected subprotocol 可查询；握手头仍不支持受控自定义 Origin/Authorization/Cookie。
- 低层 `WebSocketClient` public 入口沿用 `PASSIVE_LEVEL` 约束。

### HTTP/2 与 HPACK

关键文件：

- `src/KernelHttpLib/http2/Http2Connection.cpp`
- `src/KernelHttpLib/http2/Http2Frame.cpp`
- `src/KernelHttpLib/http2/Hpack.cpp`
- `tests/http2_client_tests.cpp`
- `tests/http2_frame_tests.cpp`
- `tests/hpack_tests.cpp`

已确认的支持能力：

- 支持 ALPN/h2c、preface、SETTINGS/ACK、HEADERS/CONTINUATION、DATA、PING、GOAWAY、WINDOW_UPDATE、HPACK。
- SETTINGS ACK payload 非 0、PING payload 非 8、禁用 push 后收到 `PUSH_PROMISE` 等按 connection error 处理。
- 响应 header block 校验重复/缺失 `:status`、伪头顺序、大写 header、connection-specific header、`TE` 非 `trailers`。
- response trailers 在 DATA 后、END_STREAM 路径有测试。
- HPACK dynamic table size update 位置、header-list size、sensitive header never-indexed 有测试。
- 响应必须以 `END_STREAM`、`RST_STREAM` 或 `GOAWAY` 结束，半截响应 timeout 不返回成功。

当前边界：

- 不支持 server push。
- 不支持完整 priority/复杂多流调度。
- 不是通用 HTTP/2 multiplexing 实现。
- 请求侧 `TE` 已收紧为只允许 `trailers`。
- 响应侧已补 `content-length` 与 DATA 总长度一致性校验，不一致时发送 `RST_STREAM(PROTOCOL_ERROR)` 并失败。
- flow-control 已复核 connection window 与 stream window 的区分、`SETTINGS_INITIAL_WINDOW_SIZE` 动态调整、max frame size 和 1xx informational response。

### TLS、证书与底层传输

关键文件：

- `src/KernelHttpLib/tls/TlsConnection.cpp`
- `src/KernelHttpLib/tls/TlsHandshake13.cpp`
- `src/KernelHttpLib/tls/CertificateValidator.cpp`
- `src/KernelHttpLib/net/WskSocket.cpp`
- `src/KernelHttpLib/net/WskSync.h`
- `tests/tls_record_tests.cpp`
- `tests/khttp_tests.cpp`

已确认的支持能力：

- TLS 1.2/1.3 主路径使用自实现 record/handshake 和内核 CNG/BCrypt，不依赖 SChannel。
- TLS1.2 选择需要版本协商证据；证书错误、ALPN mismatch、网络 timeout、record 解密失败不是 TLS1.2-only 证据。
- TLS 1.3 PSK ticket 绑定 SNI/ALPN/cipher/version/lifetime；HRR 后重算 binder；0-RTT 默认关闭且要求 replay-safe。
- 证书校验覆盖有效期、SAN/CN、iPAddress SAN、EKU、KeyUsage、BasicConstraints、链签名、trust anchor、SPKI pin。
- RequireRevocationCheck 在未实现 OCSP/CRL 时返回 `STATUS_NOT_SUPPORTED`。
- WSK connect/send/receive timeout、cancel、late completion 路径有 socket 所有权与 close exactly once 设计。
- 同步 HTTP/WebSocket/TLS/证书路径要求 `PASSIVE_LEVEL`。

当前边界：

- HTTP HTTPS 默认允许 TLS1.2-TLS1.3 时，HTTP engine/HttpsClient 与 WebSocket 一样，只在 TLS 层给出版本协商证据时用新 socket 做 TLS1.2 confirmation；证书错误、ALPN mismatch、timeout、record 解密失败不会触发确认。
- TLS 1.3 post-handshake 目前主要消费 NewSessionTicket，KeyUpdate 和 post-handshake auth 未完整支持。
- ALPN 存储和解析按当前 `h2`/`http/1.1` 子集设计，不是 RFC 7301 通用长度能力。
- 不支持 TLS client certificate authentication。
- 不支持 CBC、ChaCha20-Poly1305 等更多 cipher suite。
- 不实现完整 OCSP/CRL、完整 Name Constraints 和 IDNA 策略。
- DNS cache 固定 TTL，不读取 DNS TTL；`ResolveAll` 是同步 DNS resolve 边界，一旦进入底层解析不承诺取消。
- WSK send/receive 每次分配 nonpaged buffer/MDL 并复制，生命周期安全但高频压力需要 Driver Verifier/PoolMon 验证。
- WSK timeout/cancel/late completion 的所有权边界已有实现注释和历史审计记录；仍建议用 Driver Verifier/PoolMon 做真实内核场景验证。

真实内核验证步骤：

1. 开启 Driver Verifier：Special Pool、Pool Tracking、I/O Verification、Force IRQL Checking、Deadlock Detection，目标为加载 KernelHttp 的测试驱动。
2. 用可控测试驱动触发 connect/send/receive timeout、调用方 cancel、TLS handshake cancel、driver unload 前 session drain/close。
3. 同时用 PoolMon 观察项目 pool tag，确认 timeout/cancel/late completion 后 nonpaged pool 不持续增长。
4. 在 kernel debugger 中断点观察 `WskSocket::CloseAfterCancelledOperation`、`WskSocket::CloseOwnedSocket` 和 WSK completion cleanup，确认 native socket close exactly once。
5. 重复 IPv4/IPv6、TLS SNI/ALPN/证书策略变化场景，确认连接池不跨 TLS 身份复用。

## 标准和成熟实现参照

- RFC 9110 / RFC 9112：HTTP 语义与 HTTP/1.1 message framing，尤其 `Transfer-Encoding`、`Content-Length`、连接复用、upgrade 和 trailer 语义。
- RFC 6455：WebSocket opening handshake、mask、payload length、fragmentation、control frame、close code、UTF-8 文本语义。
- RFC 9113 / RFC 7541：HTTP/2 frame、stream state、connection error、HPACK dynamic table 与 header block 约束。
- RFC 8446 / RFC 5246：TLS 1.3/1.2 handshake、HRR transcript、PSK binder、0-RTT replay 风险、downgrade protection。
- RFC 5280：证书链、SAN、KeyUsage、BasicConstraints、Name Constraints、revocation 相关策略。
- curl/libcurl：`CURLOPT_FOLLOWLOCATION`、`CURLOPT_UNRESTRICTED_AUTH`、`curl_ws_recv`/`curl_ws_send` 和 `lib/ws.c` 提供成熟客户端行为参照。curl 的 WebSocket API暴露 frame metadata，本项目默认聚合完整消息，因此这是明确 API 边界而不是同等能力。

## 建议

- 当前 README/API 的“协议子集”表述应保留，不要改成“完整 HTTP/WebSocket/TLS 栈”。
- 若目标是内核客户端主路径稳定，下一步优先保持用户态协议矩阵、Debug x64 构建、Driver Verifier/PoolMon 真实内核验证同步通过。
- 若目标是协议宽度补全，按 `docs/superpowers/plans/2026-06-09-http-websocket-protocol-completion-plan.md` 执行，并逐项补测试、实现、文档。
