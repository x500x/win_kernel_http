# HTTP/WebSocket Protocol Completion Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在保持 Windows kernel 主路径和现有协议子集正确性的基础上，优先补齐已支持子集内的互通/严格性缺口，再把仍未实现的 HTTP/WebSocket/TLS/HTTP2 协议宽度按可测试任务补齐，或明确保留为非目标。

**Architecture:** 先锁定现有支持子集的回归矩阵，再修 HTTP HTTPS TLS1.2 confirmation、HTTP/2 严格性、HTTP/1.1 兼容性语法、WebSocket 公开控制帧/关闭握手等已暴露能力缺口；随后按协议层分块扩展：HTTP/1.1 request/trailer、WebSocket frame API/extension、TLS/证书、HTTP/2 multiplexing、WSK 内核场景验证。每个扩展都必须先写负向/正向测试，再实现，不引入 WinHTTP、WinINet、SChannel，也不把失败重试作为架构手段。

**Tech Stack:** Windows kernel C++ under `/kernel`, WSK, kernel CNG/BCrypt, custom HTTP/1.x parser, custom WebSocket, custom TLS 1.2/1.3, custom HTTP/2+HPACK, MSBuild/user-mode protocol tests through `pwsh`. 不运行被禁止的 `tests/integration/https_smoke.ps1 -SkipDriverBuild`。

---

## 执行规则

- 使用 `pwsh`，不要使用 `powershell`。
- 不提交；只有用户明确要求时才 git commit。
- 代码变更后必须运行对应用户态测试和 Debug x64 构建，warning-as-error 必须保持 0 warning。
- 新增能力必须有 API 契约、上限、内存所有权和错误码说明。
- 所有网络/TLS/证书同步路径继续要求 `PASSIVE_LEVEL`。
- 不把 optional 能力写成已支持，除非测试、代码、文档都完成。

## 文件地图

- 说明文档：`docs/plans/2026-06-09-http-websocket-protocol-recheck-notes.md`
- HTTP/1.1：`include/KernelHttp/http/HttpRequest.h`、`src/KernelHttpLib/http/HttpRequest.cpp`、`src/KernelHttpLib/http/HttpParser.cpp`、`src/KernelHttpLib/http/HttpTransferCoding.cpp`、`src/KernelHttpLib/engine/HttpEngine.cpp`、`tests/http_parser_tests.cpp`、`tests/khttp_tests.cpp`
- WebSocket：`include/KernelHttp/websocket/WebSocketFrame.h`、`include/KernelHttp/client/WebSocketClient.h`、`src/KernelHttpLib/websocket/WebSocketFrame.cpp`、`src/KernelHttpLib/client/WebSocketClient.cpp`、`src/KernelHttpLib/engine/WsEngine.cpp`、`tests/websocket_frame_tests.cpp`、`tests/websocket_client_tests.cpp`
- TLS/证书：`include/KernelHttp/tls/TlsConnection.h`、`include/KernelHttp/tls/CertificateValidator.h`、`src/KernelHttpLib/tls/TlsConnection.cpp`、`src/KernelHttpLib/tls/TlsHandshake13.cpp`、`src/KernelHttpLib/tls/CertificateValidator.cpp`、`tests/tls_record_tests.cpp`
- HTTP/2/HPACK：`include/KernelHttp/http2/Http2Connection.h`、`include/KernelHttp/http2/Hpack.h`、`src/KernelHttpLib/http2/Http2Connection.cpp`、`src/KernelHttpLib/http2/Hpack.cpp`、`tests/http2_client_tests.cpp`、`tests/hpack_tests.cpp`
- WSK/底层：`include/KernelHttp/net/WskClient.h`、`include/KernelHttp/net/WskSocket.h`、`src/KernelHttpLib/net/WskClient.cpp`、`src/KernelHttpLib/net/WskSocket.cpp`、`src/KernelHttpLib/net/WskSync.h`、`tests/khttp_tests.cpp`
- 文档：`README.md`、`README_en.md`、`docs/api-overview.md`、`docs/high-level-api.md`、`docs/low-level-api.md`

## Chunk 1: Baseline Contract And Verification

### Task 1: 锁定当前支持子集

**Files:**
- Modify if wording drifts: `README.md`
- Modify if wording drifts: `README_en.md`
- Modify if wording drifts: `docs/api-overview.md`
- Modify if wording drifts: `docs/high-level-api.md`
- Reference: `docs/plans/2026-06-09-http-websocket-protocol-recheck-notes.md`

- [ ] 检查所有文档是否继续使用“现代内核客户端协议子集”，不宣称完整 RFC optional 全量。
- [ ] 保留当前 unsupported 表：chunked upload、trailer exposure、proxy/CONNECT、WebSocket extensions/fragment callback、TLS client cert/OCSP/CRL/IDNA、HTTP/2 push/priority/full multiplexing。
- [ ] 明确这些边界不是隐式成功路径；触发时必须返回固定错误或拒绝握手。
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\websocket_frame_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\websocket_client_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http2_frame_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\hpack_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http2_client_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\tls_record_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\high_level_api_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\khttp_tests.exe'`
- [ ] Expected: all protocol tests pass.

### Task 2: 建立协议缺口决策表

**Files:**
- Create: `docs/plans/2026-06-09-http-websocket-protocol-scope-decisions.md`

- [ ] 为每个缺口写明决策：实现、延期、非目标。
- [ ] 每项记录 RFC/curl/内核约束依据、API 影响、测试入口、预期错误码。
- [ ] 将“实现”项映射到下方 chunk。
- [ ] 不提交文档。

## Chunk 1A: Supported-Subset Remediation

### Task 2.1: HTTP HTTPS TLS1.2 confirmation parity

**Files:**
- Modify: `src/KernelHttpLib/client/HttpsClient.cpp`
- Modify if engine owns retry: `src/KernelHttpLib/engine/HttpEngine.cpp`
- Modify if helper is shared: `include/KernelHttp/tls/TlsConnection.h`
- Test: `tests/tls_record_tests.cpp`
- Test: `tests/khttp_tests.cpp`
- Test: `tests/high_level_api_tests.cpp`

- [ ] 写 failing tests：默认 TLS min=1.2/max=1.3 连接 TLS1.2-only server 时，HTTP HTTPS 路径能用新 socket 确认 TLS1.2；证书错误、ALPN mismatch、timeout、record 解密失败不得触发 TLS1.2 confirmation。
- [ ] 复用或抽出 WebSocket 已有 `VersionNegotiation` 分类逻辑，避免 HTTP 和 WebSocket 各自漂移。
- [ ] confirmation 必须使用新 WSK socket；不能在已经失败的 TLS1.3 transport 上继续写 TLS1.2。
- [ ] 确认成功后仍执行原有 SNI、ALPN、证书、pin、trust store 策略。
- [ ] 确认失败时保留原始错误和 diagnostic，不把非版本错误改写为兼容性成功。
- [ ] 运行 `tls_record_tests.exe`、`khttp_tests.exe`、`high_level_api_tests.exe`。

### Task 2.2: HTTP/2 request/response strictness

**Files:**
- Modify: `src/KernelHttpLib/client/Http2Client.cpp`
- Modify: `src/KernelHttpLib/http2/Http2Connection.cpp`
- Modify: `include/KernelHttp/http2/Http2Connection.h`
- Test: `tests/http2_client_tests.cpp`
- Test: `tests/http2_frame_tests.cpp`

- [ ] 写 failing tests：请求 header 中 `TE` 只能是 `trailers`；其他值返回 `STATUS_INVALID_PARAMETER` 或明确协议错误。
- [ ] 写 tests：响应 `content-length` 与 DATA 总长度不一致时发送 `RST_STREAM(PROTOCOL_ERROR)` 并失败。
- [ ] 写 tests：`SETTINGS_INITIAL_WINDOW_SIZE` 只影响 stream window，不覆盖 connection send window；窗口减小时 active stream 正确调整。
- [ ] 写 tests：1xx informational response 后继续等待最终 response；非法 1xx/trailer 顺序返回协议错误。
- [ ] 复核 `peerSettings_.MaxFrameSize` 与 `Http2DefaultMaxFrameSize` 的关系，避免合法大 frame 被固定 16K payload 缓冲误伤或越界。
- [ ] 运行 `http2_client_tests.exe`、`http2_frame_tests.exe`、`hpack_tests.exe`。

### Task 2.3: HTTP/1.1 compatibility grammar decisions

**Files:**
- Modify if behavior changes: `src/KernelHttpLib/http/HttpRequest.cpp`
- Modify if behavior changes: `src/KernelHttpLib/http/HttpParser.cpp`
- Modify if behavior changes: `src/KernelHttpLib/http/HttpTransferCoding.cpp`
- Test: `tests/http_parser_tests.cpp`
- Doc: `docs/high-level-api.md`

- [ ] 写 tests：空 header value 是否允许；若继续拒绝，文档和错误码必须明确。
- [ ] 写 tests：`Content-Length: 5, 5` 等等值列表是否接受；冲突列表仍拒绝。
- [ ] 写 tests：transfer-coding 参数和 chunk extension；决定支持 RFC 语法还是明确拒绝。
- [ ] 如果支持，所有解析都要保持 bounded buffer 与 overflow 检查。
- [ ] 运行 `http_parser_tests.exe` 和 `khttp_tests.exe`。

### Task 2.4: WebSocket public control-frame and close semantics

**Files:**
- Modify: `include/KernelHttp/khttp/WebSocket.h`
- Modify: `include/KernelHttp/engine/Engine.h`
- Modify: `src/KernelHttpLib/client/WebSocketClient.cpp`
- Modify: `src/KernelHttpLib/engine/WsEngine.cpp`
- Modify: `src/KernelHttpLib/khttp/WebSocket.cpp`
- Test: `tests/websocket_client_tests.cpp`
- Test: `tests/high_level_api_tests.cpp`

- [ ] 增加 `WsSendPing`、`WsSendPong`、`WsCloseEx(code, reason)` 或等价 API，先写 API tests。
- [ ] `AutoReplyPing=false` 时，调用方收到 Ping 后必须能显式发送 Pong。
- [ ] 主动 close 支持有界等待对端 close；超时后关闭 TCP/TLS，不能无限等待。
- [ ] selected subprotocol 增加只读查询接口，或文档明确无法读取。
- [ ] 低层 `WebSocketClient` public 入口补 PASSIVE_LEVEL 检查；若保持 internal-only，头文件和文档明确。
- [ ] 收紧 `ValidateServerHandshake` 对 `HttpResponse` 版本的校验，避免直接构造默认 0.0 response 通过。
- [ ] 运行 `websocket_frame_tests.exe`、`websocket_client_tests.exe`、`high_level_api_tests.exe`。

### Task 2.5: Kernel transport and diagnostics hardening

**Files:**
- Modify: `src/KernelHttpLib/net/WskClient.cpp`
- Modify: `src/KernelHttpLib/net/WskSocket.cpp`
- Modify: `src/KernelHttpLib/net/WskSync.h`
- Test if added: `tests/wsk_socket_tests.cpp`
- Test: `tests/khttp_tests.cpp`

- [ ] 为 `ResolveAll` 增加取消 token 或明确同步 DNS resolve 不可取消的 API 边界。
- [ ] 评估 DNS cache TTL：保留固定 TTL 时文档化；若读取 DNS TTL，先设计 WSK 可获得信息和上限。
- [ ] 建 fake WSK tests：connect/send/receive cancel、late completion、driver unload 前 cleanup、close exactly once。
- [ ] 评估 per-connection/workspace 常驻 nonpaged MDL buffer，减少高频 send/receive 分配和复制。
- [ ] 用 Driver Verifier/PoolMon 设计真实内核验证步骤，记录在说明文档。

## Chunk 2: HTTP/1.1 Width

### Task 3: 支持 request chunked upload

**Files:**
- Modify: `include/KernelHttp/http/HttpRequest.h`
- Modify: `src/KernelHttpLib/http/HttpRequest.cpp`
- Modify: `src/KernelHttpLib/engine/HttpEngine.cpp`
- Modify if public API is added: `include/KernelHttp/khttp/Types.h`
- Test: `tests/http_parser_tests.cpp`
- Test: `tests/khttp_tests.cpp`

- [ ] 先写 failing tests：unknown body length 使用 chunked request；固定 body 仍默认 `Content-Length`；用户手写 `Transfer-Encoding` 仍拒绝。
- [ ] 设计显式 send option，例如 `RequestBodyMode::Chunked`，不自动猜测。
- [ ] chunk 编码每个 chunk size、CRLF、final zero chunk；禁止未声明 trailer。
- [ ] 若支持 request trailer，先设计固定容量 trailer 结构和 forbidden field 校验。
- [ ] 运行 `http_parser_tests.exe` 与 `khttp_tests.exe`。

### Task 4: 暴露 response trailer API

**Files:**
- Modify: `include/KernelHttp/http/HttpResponse.h`
- Modify: `src/KernelHttpLib/http/HttpParser.cpp`
- Modify: `include/KernelHttp/khttp/Response.h`
- Modify: `src/KernelHttpLib/khttp/Response.cpp`
- Test: `tests/http_parser_tests.cpp`
- Test: `tests/khttp_tests.cpp`

- [ ] 先写 tests：合法 trailer 暴露；非法 field-name 拒绝；framing/routing/auth forbidden trailer 拒绝。
- [ ] 增加 bounded trailer 存储，避免无界 nonpaged pool 增长。
- [ ] API 返回 trailer count、name/value view，不复制到不受控缓冲。
- [ ] 文档说明 trailer 只在 chunked 完成后可见。
- [ ] 运行 `http_parser_tests.exe` 与 `khttp_tests.exe`。

### Task 5: 评估并实现 HTTP proxy/CONNECT

**Files:**
- Modify if accepted: `include/KernelHttp/engine/Engine.h`
- Modify if accepted: `src/KernelHttpLib/engine/HttpEngine.cpp`
- Modify if accepted: `src/KernelHttpLib/client/HttpsClient.cpp`
- Test: `tests/khttp_tests.cpp`
- Doc: `docs/high-level-api.md`

- [ ] 决定 proxy/CONNECT 是否进入内核客户端主路径；若仍非目标，文档保留拒绝。
- [ ] 若实现，先写 tests：HTTP absolute-form request、HTTPS CONNECT、proxy auth header 不跨 origin 泄露。
- [ ] CONNECT 建立后 TLS 仍使用目标 host 的 SNI/证书校验，不使用 proxy host。
- [ ] WebSocket over proxy 仅在 CONNECT/TLS 路径清晰后实现。
- [ ] 运行 `khttp_tests.exe` 和相关 high-level 测试。

## Chunk 3: WebSocket Width

### Task 6: 公开 frame metadata / 分片回调能力

**Files:**
- Modify: `include/KernelHttp/client/WebSocketClient.h`
- Modify: `include/KernelHttp/khttp/WebSocket.h`
- Modify: `src/KernelHttpLib/client/WebSocketClient.cpp`
- Modify: `src/KernelHttpLib/engine/WsEngine.cpp`
- Test: `tests/websocket_client_tests.cpp`
- Test: `tests/high_level_api_tests.cpp`

- [ ] 参考 curl `curl_ws_recv` 的 frame metadata 思路，设计本项目自己的 bounded metadata 结构。
- [ ] 先写 tests：应用可选择完整消息模式或逐 frame 模式；控制帧穿插不破坏状态。
- [ ] 默认仍保持完整消息聚合，避免破坏现有 API。
- [ ] 分片模式必须暴露 FIN/opcode/payload length/close code，并保持 server mask/RSV/length 严格校验。
- [ ] 运行 WebSocket 相关测试。

### Task 7: 设计 WebSocket extension/permessage-deflate

**Files:**
- Modify if accepted: `include/KernelHttp/websocket/WebSocketFrame.h`
- Modify if accepted: `src/KernelHttpLib/websocket/WebSocketFrame.cpp`
- Modify if accepted: `src/KernelHttpLib/client/WebSocketClient.cpp`
- Test: `tests/websocket_frame_tests.cpp`
- Test: `tests/websocket_client_tests.cpp`

- [ ] 先写设计文档，明确 RSV1、context takeover、window bits、内核内存上限和压缩炸弹防护。
- [ ] 若不能给出 bounded memory 和 CPU 策略，继续把 extension 作为非目标。
- [ ] 若实现，握手只发送明确支持的 extension 参数，拒绝未请求或参数不匹配的扩展。
- [ ] inbound decompression 在消息边界执行，超限按 1009 关闭。
- [ ] 运行 WebSocket 测试和 Debug x64 构建。

## Chunk 4: TLS And Certificate Width

### Task 8: TLS client certificate authentication

**Files:**
- Modify: `include/KernelHttp/tls/TlsConnection.h`
- Modify: `src/KernelHttpLib/tls/TlsConnection.cpp`
- Modify: `src/KernelHttpLib/tls/TlsHandshake12.cpp`
- Modify: `src/KernelHttpLib/tls/TlsHandshake13.cpp`
- Test: `tests/tls_record_tests.cpp`

- [ ] 先写 tests：TLS 1.2/1.3 CertificateRequest，空证书拒绝/允许策略，CertificateVerify 签名。
- [ ] 设计 kernel key handle 生命周期，私钥不可导出，CNG 签名必须符合 IRQL 约束。
- [ ] API 只接受明确的 client certificate credential，不自动从系统 store 搜索。
- [ ] TLS 1.3 transcript 和 TLS 1.2 handshake hash 必须分别校验。
- [ ] 运行 `tls_record_tests.exe`。

### Task 9: OCSP/CRL 与 Name Constraints/IDNA

**Files:**
- Modify: `include/KernelHttp/tls/CertificateValidator.h`
- Modify: `src/KernelHttpLib/tls/CertificateValidator.cpp`
- Test: `tests/tls_record_tests.cpp`
- Doc: `docs/high-level-api.md`

- [ ] Name Constraints：先补 DNS/IP permitted/excluded 正向与负向测试。
- [ ] IDNA：决定 IDNA2008/UTS #46 策略；若不实现，继续拒绝非 ASCII host。
- [ ] OCSP/CRL：单独设计缓存、超时、stapling、网络重入和撤销软/硬失败策略。
- [ ] RequireRevocationCheck 在实现前继续返回 `STATUS_NOT_SUPPORTED`，不得静默放行。
- [ ] 运行 `tls_record_tests.exe`。

### Task 10: 扩展 cipher suite

**Files:**
- Modify: `include/KernelHttp/tls/TlsContext.h`
- Modify: `src/KernelHttpLib/tls/TlsConnection.cpp`
- Modify: `src/KernelHttpLib/tls/TlsRecord.cpp`
- Test: `tests/tls_record_tests.cpp`

- [ ] 决定是否支持 ChaCha20-Poly1305；若内核 CNG 支持和性能收益不足，保持非目标。
- [ ] 不优先恢复 CBC；若必须支持，需先设计 padding oracle、MAC-then-encrypt、record splitting 等风险控制。
- [ ] 所有新增 cipher 必须有 record protect/unprotect、handshake negotiation、negative alert tests。
- [ ] 运行 `tls_record_tests.exe`。

## Chunk 5: HTTP/2 Width

### Task 11: 完整多流调度与 flow control

**Files:**
- Modify: `include/KernelHttp/http2/Http2Connection.h`
- Modify: `src/KernelHttpLib/http2/Http2Connection.cpp`
- Modify: `src/KernelHttpLib/http2/Http2Stream.cpp`
- Test: `tests/http2_client_tests.cpp`

- [ ] 先写 tests：两个并发 stream 交错 HEADERS/DATA、独立 RST_STREAM、连接级 WINDOW_UPDATE。
- [ ] 设计 bounded stream table 和 per-stream buffer 上限。
- [ ] 不让非目标 stream 的数据无限堆积。
- [ ] `MAX_CONCURRENT_STREAMS`、`INITIAL_WINDOW_SIZE` 动态更新影响已有 stream。
- [ ] 运行 `http2_client_tests.exe`。

### Task 12: priority/server push 策略

**Files:**
- Modify if accepted: `src/KernelHttpLib/http2/Http2Connection.cpp`
- Test: `tests/http2_client_tests.cpp`
- Doc: `README.md`
- Doc: `README_en.md`

- [ ] 决定是否实现 priority；若不实现，确认忽略/错误处理符合当前 RFC 边界。
- [ ] server push 默认继续禁用；若实现，必须有缓存、取消、authority 校验和内存上限。
- [ ] 保持收到禁用 `PUSH_PROMISE` 时 connection protocol error。
- [ ] 运行 HTTP/2 测试。

## Chunk 6: Kernel Transport Hardening

### Task 13: fake WSK late-completion 测试

**Files:**
- Create if needed: `tests/wsk_socket_tests.cpp`
- Modify if project file changes: `src/KernelHttpLib/KernelHttpLib.vcxproj`
- Modify if needed: `src/KernelHttpLib/net/WskSocket.cpp`
- Modify if needed: `src/KernelHttpLib/net/WskSync.h`

- [ ] 建 fake WSK provider，覆盖 connect/send/receive timeout、caller cancel、late success、late failure、close exactly once。
- [ ] 验证 completion-owned cleanup 不泄漏 native socket、IRP、buffer。
- [ ] 验证 cancel 后不再把连接放回 pool。
- [ ] 运行新增测试和 `khttp_tests.exe`。

### Task 14: DNS cache 与连接身份

**Files:**
- Modify: `src/KernelHttpLib/net/WskClient.cpp`
- Modify: `src/KernelHttpLib/engine/ConnectionPool.cpp`
- Test: `tests/khttp_tests.cpp`

- [ ] 增加 tests：DNS cache TTL、IPv4/IPv6 family、host/SNI/ALPN/trust store/pin 改变后不复用错误连接。
- [ ] 保持连接池 key 包含 TLS 身份，不因 DNS cache 复用跨身份连接。
- [ ] DNS cache 失效不影响已建立连接的证书 host 校验。
- [ ] 运行 `khttp_tests.exe`。

## Chunk 7: Final Verification

### Task 15: 全量用户态协议测试

**Files:**
- No source edits unless failures require fixes.

- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\websocket_frame_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\websocket_client_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http2_frame_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\hpack_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http2_client_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\tls_record_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\high_level_api_tests.exe'`
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\khttp_tests.exe'`
- [ ] Do not run the forbidden integration smoke command.

### Task 16: Debug x64 构建

**Files:**
- No source edits unless failures require fixes.

- [ ] Run without artificial timeout: `pwsh -NoLogo -NoProfile -Command '$ErrorActionPreference = "Stop"; $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"; $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath; if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($vsPath)) { throw "Visual Studio with VC tools was not found." }; $devShell = Join-Path $vsPath.Trim() "Common7\Tools\Launch-VsDevShell.ps1"; & $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null; & msbuild.exe .\KernelHttp.sln /p:Configuration=Debug /p:Platform=x64 /m; exit $LASTEXITCODE'`
- [ ] Confirm `WarningLevel=EnableAllWarnings` and `TreatWarningAsError=true`.
- [ ] Confirm exceptions and RTTI remain disabled.
- [ ] Do not commit unless explicitly requested.

## 完成标准

- 已声明支持的协议子集仍全部通过测试和 Debug x64 构建。
- 每个新增协议宽度能力都有正向/负向测试、bounded memory 策略、明确 API 和文档。
- 未实现能力保持显式拒绝或文档非目标，不静默成功。
- 不引入 WinHTTP、WinINet、SChannel 作为内核主路径。
