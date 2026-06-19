# 路线图与非目标 / Roadmap & Non-Goals

[English](#english) | 简体中文

---

## 简体中文

明确划清边界有助于正确使用。以下为**有意不做**或**延期**的能力，以及未来改进方向。

### 明确的非目标

**HTTP/1.1**
- `Expect: 100-continue`（主动拒绝）
- HTTP 管线化（串行请求/响应，刻意不实现）
- TRACE 方法
- 流式请求体上传（chunked 一次性编码）
- 流式响应回调（响应先缓冲再交付）
- `obs-fold` 折行（拒绝而非规范化）

**HTTP/2**
- 高层 `khttp` 的 h2 连接池级复用（低层 `Http2Connection` 已支持多活动流基础与交错帧分发）
- 发送 PRIORITY 帧（合法地省略）
- 主动 PING 保活
- 高层 khttp 暴露 h2c（仅底层 `Http2Client`）

**WebSocket**（注：分片发送 `kws::SendContinuation` 与接收分片回调 `ReceiveOptions.OnMessage` **已支持**）
- permessage-deflate（RFC 7692）
- 高层 `kws` 自动 WebSocket over HTTP/2（RFC 8441；低层 HTTP/2 extended CONNECT tunnel 基础已支持）
- 握手 redirect / 401 跟随

**代理**
- 高层 Session 全局代理配置（低层 `HttpsClient` 已支持显式 HTTP/1.1 CONNECT 代理隧道）

**TLS**
- TLS 1.2 renegotiation（仅信令，未实现）
- 在线撤销抓取（OCSP/CRL 网络拉取）——内核态刻意省略，支持静态撤销条目
- 0-RTT / early data（已实现，默认关闭，需显式 replay-safe）

**其它**
- HTTP/3 / QUIC
- HTTP 服务端 / 入站 request parser

### 默认关闭、可显式开启

- TLS 1.2 RSA key exchange / CBC / renegotiation / SHA-1 签名（需 `TlsPolicy` + `CompatibilityExplicit`）
- Post-handshake client auth
- 强制撤销检查
- TLS 1.3 0-RTT

### 未来改进方向（持续）

- 建立独立于调用方容量的**每调用 / 每记录 / 每连接资源硬上限**。
- 热路径减少重复分配（分配复用 / lookaside 式池化）。
- 将低层 h2 多活动流与 RFC 8441 tunnel 能力接入高层连接池 / WebSocket 自动选择（需单独设计 API 与并发语义）。

> 这些是对**当前公开行为**的描述，便于评估适用性；不代表内部审计细节。能力现状见 [能力边界](capability-matrix.md)。

---

## English

Explicit boundaries help correct usage. The following are intentionally **out of scope** or **deferred**, plus ongoing improvement directions.

**Non-goals.** HTTP/1.1: `Expect: 100-continue` (rejected), pipelining, TRACE, streaming upload, streaming response callbacks, obs-fold (rejected). HTTP/2: high-level `khttp` h2 connection-pool reuse (the low-level connection already has active-stream routing), PRIORITY frames, proactive PING, h2c at high-level khttp. WebSocket (fragment send + receive-fragment callback are supported): permessage-deflate (RFC 7692), automatic high-level WebSocket over HTTP/2 (RFC 8441 low-level tunnel primitives exist), handshake redirect/401 following. Proxy: session-wide high-level proxy configuration (low-level `HttpsClient` has explicit HTTP/1.1 CONNECT tunneling). TLS: TLS 1.2 renegotiation (signaling only), online OCSP/CRL fetch (omitted in kernel; static entries supported), 0-RTT (implemented, off by default). Other: HTTP/3·QUIC, server/inbound request parsing.

**Off by default, explicitly enable.** TLS 1.2 RSA kx / CBC / renegotiation / SHA-1 (via `TlsPolicy` + `CompatibilityExplicit`), post-handshake client auth, required revocation check, TLS 1.3 0-RTT.

**Future directions.** Per-call / per-record / per-connection resource hard limits independent of caller capacities; reducing repeated allocations on hot paths (allocation reuse / lookaside pooling); lifting the low-level h2 active-stream and RFC 8441 tunnel primitives into high-level connection pooling / WebSocket automatic selection after a separate API/concurrency design.

This describes current public behavior for fit assessment; see [Capability Matrix](capability-matrix.md) for the current state.
