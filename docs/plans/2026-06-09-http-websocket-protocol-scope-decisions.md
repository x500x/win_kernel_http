# HTTP/WebSocket 协议范围决策表

日期：2026-06-09

本文档配套 `docs/plans/2026-06-09-http-websocket-protocol-recheck-notes.md` 和
`docs/superpowers/plans/2026-06-09-http-websocket-protocol-completion-plan.md`。
决策口径是：已声明支持的客户端协议子集必须严格、可测试；RFC optional 或部署宽度能力只有在能给出内核内存上限、明确 API 契约和正/负向测试时才进入实现，否则保留为显式非目标或延期。

## 决策总览

| 缺口 | 决策 | 依据 | API 影响 | 预期错误码/行为 | 测试入口 | 映射 |
|---|---|---|---|---|---|---|
| HTTP HTTPS TLS1.2 confirmation parity | 实现 | WebSocket 路径已有版本协商确认；HTTP HTTPS 默认 TLS1.2-TLS1.3 时需要同等互通性 | 不新增公开 API；复用 TLS 配置 | 仅版本协商证据触发新 socket TLS1.2 确认；证书/ALPN/timeout/record 错误保留原错误 | `tests/tls_record_tests.cpp`, `tests/khttp_tests.cpp`, `tests/high_level_api_tests.cpp` | Chunk 1A Task 2.1 |
| HTTP/2 请求侧 `TE` 校验 | 实现 | RFC 9113 只允许 `TE: trailers` | 不新增 API | 非 `trailers` 返回 `STATUS_INVALID_PARAMETER` 或 stream protocol error | `tests/http2_client_tests.cpp` | Chunk 1A Task 2.2 |
| HTTP/2 response content-length/DATA 一致性 | 实现 | RFC 9113 要求 `content-length` 与 DATA 长度一致 | 不新增 API | 发送 `RST_STREAM(PROTOCOL_ERROR)` 并返回协议错误 | `tests/http2_client_tests.cpp` | Chunk 1A Task 2.2 |
| HTTP/2 dynamic flow-control 语义 | 实现 | `SETTINGS_INITIAL_WINDOW_SIZE` 只改变 stream window，connection window 独立 | 不新增 API | 非法窗口返回 flow-control/protocol error | `tests/http2_client_tests.cpp` | Chunk 1A Task 2.2 |
| HTTP/2 informational response 顺序 | 实现 | 1xx 后必须继续等待最终响应；trailer 只能在最终响应 DATA 后 | 不新增 API | 非法 1xx/trailer 顺序返回协议错误 | `tests/http2_client_tests.cpp` | Chunk 1A Task 2.2 |
| HTTP/1.1 空 header value | 实现为允许 | RFC 9110 field value 可为空；内核实现可用长度对表达 | 不新增 API | 解析成功；字段值长度为 0 | `tests/http_parser_tests.cpp` | Chunk 1A Task 2.3 |
| HTTP/1.1 `Content-Length` 等值列表 | 实现 | RFC 9110 允许等值列表归一；冲突值必须拒绝 | 不新增 API | `5, 5` 接受为 5；`5, 6` 返回协议错误 | `tests/http_parser_tests.cpp` | Chunk 1A Task 2.3 |
| transfer-coding 参数与 chunk extension | 实现有限语法 | 互通常见；必须保持 bounded parser 与 overflow 检查 | 不新增 API | 合法参数/extension 消费；非法格式拒绝 | `tests/http_parser_tests.cpp` | Chunk 1A Task 2.3 |
| WebSocket public Ping/Pong/CloseEx | 实现 | RFC 6455 控制帧是公开 WebSocket 能力；`AutoReplyPing=false` 需要显式 Pong | 新增 `WsSendPing`、`WsSendPong`、`WsCloseEx` | 非法 close code/reason 过长返回 `STATUS_INVALID_PARAMETER`; 关闭等待超时后关闭传输 | `tests/websocket_client_tests.cpp`, `tests/high_level_api_tests.cpp` | Chunk 1A Task 2.4 |
| selected subprotocol 查询 | 实现 | 调用方需要知道服务端选择结果 | 新增只读查询接口 | 未协商返回空视图 | `tests/websocket_client_tests.cpp`, `tests/high_level_api_tests.cpp` | Chunk 1A Task 2.4 |
| WSK `ResolveAll` 取消 | 延期，文档明确同步 DNS resolve 不可取消 | 当前 WSK 同步 resolve 能力受内核调用边界限制；取消语义需要独立设计 | 文档补边界 | resolve 开始后不承诺取消；调用方使用超时控制上层流程 | `tests/khttp_tests.cpp` 文档/边界测试 | Chunk 1A Task 2.5 |
| DNS cache TTL | 延期，保留固定 TTL 并文档化 | WSK 可获得 TTL 信息需单独验证；当前固定 TTL 更可测 | 文档补边界 | TTL 到期后重新 resolve；不读取 DNS 记录 TTL | `tests/khttp_tests.cpp` | Chunk 1A Task 2.5 / Chunk 6 Task 14 |
| fake WSK late-completion tests | 延期，保留实现注释和真实内核验证步骤 | socket 所有权是内核路径硬边界；当前用户态 harness 难以直接覆盖 WSK IRP late completion | 暂不新增 API | close exactly once；cancel 后不回池；用 Driver Verifier/PoolMon 验证 | `docs/api-overview.md`, `docs/plans/2026-06-09-http-websocket-protocol-recheck-notes.md` | Chunk 1A Task 2.5 / Chunk 6 Task 13 |
| request chunked upload | 实现 | HTTP/1.1 常见互通能力，需显式 opt-in 防止误用 | 新增显式 request body mode | 未声明 trailer；用户手写 `Transfer-Encoding` 仍拒绝 | `tests/http_parser_tests.cpp`, `tests/khttp_tests.cpp` | Chunk 2 Task 3 |
| response trailer API | 实现 | parser 已校验/消费 trailer；公开 bounded view 可提升协议完整性 | 新增 trailer count/name/value 只读 accessor | trailer 仅在 chunked 完成后可见；非法/forbidden trailer 拒绝 | `tests/http_parser_tests.cpp`, `tests/khttp_tests.cpp` | Chunk 2 Task 4 |
| HTTP proxy/CONNECT | 非目标 | 代理认证、跨 origin header 和 CONNECT 后 TLS 身份需要独立产品策略；当前内核主路径不要求 | 文档保留非目标 | 配置/输入触发返回 `STATUS_NOT_SUPPORTED` | `tests/khttp_tests.cpp` 文档/拒绝测试 | Chunk 2 Task 5 |
| WebSocket frame metadata/fragment callback | 延期 | 需要稳定 callback ABI 与 bounded buffering；不影响当前完整消息模式 | 文档保留受限；不新增 ABI | 继续聚合完整消息 | `tests/websocket_client_tests.cpp` | Chunk 3 Task 6 |
| WebSocket permessage-deflate | 非目标 | 内核内存和 CPU 上限、压缩炸弹防护、context takeover 策略需单独设计；当前不协商扩展 | 文档保留非目标 | 服务端返回未请求扩展时拒绝握手 | `tests/websocket_frame_tests.cpp`, `tests/websocket_client_tests.cpp` | Chunk 3 Task 7 |
| TLS client certificate | 延期 | kernel key handle 生命周期、CertificateVerify 和 TLS1.2/1.3 transcript 分歧需要独立设计 | 暂不新增 API | 服务器要求客户端证书时按现有策略失败或返回不支持 | `tests/tls_record_tests.cpp` | Chunk 4 Task 8 |
| OCSP/CRL | 非目标 | 网络重入、缓存、超时和软/硬失败策略复杂；当前撤销要求返回不支持 | 不新增 API | `RequireRevocationCheck` 返回 `STATUS_NOT_SUPPORTED` | `tests/tls_record_tests.cpp` | Chunk 4 Task 9 |
| Name Constraints | 延期，当前明确不支持 | 完整 RFC 5280 策略复杂，现阶段不静默放行 | 不新增 API | 触发返回 `STATUS_NOT_SUPPORTED` | `tests/tls_record_tests.cpp` | Chunk 4 Task 9 |
| IDNA | 非目标，拒绝非 ASCII host | IDNA2008/UTS #46 选择会影响证书匹配安全 | 不新增 API | 非 ASCII 主机名拒绝 | `tests/tls_record_tests.cpp` | Chunk 4 Task 9 |
| ChaCha20-Poly1305 | 非目标 | 当前 CNG/BCrypt 内核主路径优先 AES-GCM；无明确性能收益要求 | 不新增 API | cipher 不协商 | `tests/tls_record_tests.cpp` | Chunk 4 Task 10 |
| CBC cipher suites | 非目标 | padding oracle、MAC-then-encrypt、record splitting 风险高；不符合当前现代子集 | 不新增 API | cipher 不协商 | `tests/tls_record_tests.cpp` | Chunk 4 Task 10 |
| HTTP/2 full multiplexing | 延期 | 需要 bounded stream table、调度和 per-stream buffer 设计；当前客户端主路径不承诺通用多路复用 | 文档保留受限 | 复杂并发流不作为公开能力 | `tests/http2_client_tests.cpp` | Chunk 5 Task 11 |
| HTTP/2 priority | 非目标 | RFC 9113 priority 已弱化，当前客户端不需要树调度 | 文档保留非目标 | 收到 priority 信息按当前边界忽略或协议处理 | `tests/http2_client_tests.cpp` | Chunk 5 Task 12 |
| HTTP/2 server push | 非目标 | 客户端缓存、取消和 authority 校验不在内核主路径 | 文档保留非目标 | `PUSH_PROMISE` 在禁用 push 下为 protocol error | `tests/http2_client_tests.cpp` | Chunk 5 Task 12 |

## 实现顺序

1. 先补已支持子集缺口：HTTP HTTPS TLS1.2 confirmation、HTTP/2 严格性、HTTP/1.1 兼容语法、WebSocket public control-frame API。
2. 再补可控宽度能力：request chunked upload、response trailer API；fake WSK ownership/cancel 自动化测试延期，保留真实内核验证边界。
3. 保留为非目标或延期的项目只做文档和显式错误语义，不把未实现能力伪装成可用能力。

## 错误码原则

- 调用方传入不合法参数：`STATUS_INVALID_PARAMETER`。
- 能力明确未实现：`STATUS_NOT_SUPPORTED`。
- 对端协议违规：返回协议错误对应的现有 NTSTATUS，并在可用协议层发送 close/RST/GOAWAY。
- 缓冲或上限不足：`STATUS_BUFFER_TOO_SMALL` 或现有容量错误，不能截断成功。
