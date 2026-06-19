# 连接池 / Connection Pool

`engine/ConnectionPool.cpp`，`KernelHttp::engine`。每个 `KhSession` 拥有一个 `FAST_MUTEX` 保护的固定容量池。

[English](#english) | 简体中文

---

## 简体中文

### 结构与默认

- 数组 `Entries`/`Capacity`；`ActiveCount` 记在用槽，`NextConnectionId` 从 1 起。每槽 `KhPooledConnection` 缓存完整传输栈（`WskSocket`/`WskTransport`/`ITransport`/`TlsConnection`/可选 `Http2Connection`）+ `InUse`/`Connected`/`LastUsedTime`/`Key`，并记录 HTTP/2 stream 租约计数。
- 默认容量 8、最大 1024（`KhMaxConnectionPoolCapacity`）、每主机 2、空闲 30000ms。`Initialize` 要求 capacity≠0、`maxPerHost∈(0,capacity]`，否则 `STATUS_INVALID_PARAMETER`。

### 连接键匹配

`KhConnectionPoolKeysEqual`：scheme、host、port、地址族、min/max TLS 版本、证书策略/库、客户端凭据、完整 TLS `Policy`、`AutomaticAlpn`、SNI、**ALPN 字符串**、代理启用状态、代理地址与 CONNECT authority 全等。
`KhConnectionPoolKeysEqualForAutoAlpnAcquire`：当请求键 ALPN 为空、两端都 `AutomaticAlpn`、候选已记录 ALPN 时，忽略 ALPN 匹配——**让自动 ALPN 请求复用握手后回写了协商 ALPN 的连接**。

### Acquire / Release / 重试

- `KhConnectionPoolAcquire`：`ForceNew`/`NoPool` 跳过普通复用扫描；否则优先取首个 connected、空闲、键匹配的槽（空闲过期则关闭不复用）。若候选是已激活的 HTTP/2 连接且 stream 租约未达上限，可在 `InUse` 状态下继续共享同一连接并增加租约。
- **每主机配额**键更粗（仅 scheme+host+port+family，忽略 TLS/ALPN）：超限先尝试关一个同主机空闲连接，仍超 → `STATUS_INSUFFICIENT_RESOURCES`。
- 新槽优先空槽；无空槽且策略≠`NoPool` 时驱逐任一非在用槽。**`NoPool` 从不挤掉活跃连接**。
- **可回池条件**（`canReturnToPool`）：成功 + 连接可复用 + 策略 `ReuseOrCreate`。故 `ForceNew`/`NoPool` 连接 release 时总是关闭。
- **可复用判定**：状态 101、`BodyEndsOnConnectionClose`、`Connection: close`、字节未吃完、major≠1 → 不可复用（**close-delimited 与 101 永不回池**）。HTTP/1.0 须显式 keep-alive；HTTP/2 由 `Http2Connection::IsReusable()` 与 stream 租约账本决定，可在同源连接上承载多个活动 stream。
- **空闲驱逐**：仅 `IdleTimeoutMilliseconds≠0`，在 acquire 时惰性判断（无定时器）。
- **Stale fresh retry**：失败 + 复用连接 + `ReuseOrCreate` + 方法 ∈ {GET,HEAD,OPTIONS} + 状态 ∈ {连接关闭族, `STATUS_RETRY`, `STATUS_IO_TIMEOUT`} 时，关旧连接、以 `ForceNew` 重建请求**重试恰好一次**。POST/PUT/PATCH/DELETE 从不自动重放。

### 策略

| `KhConnectionPolicy` | 行为 |
|------|------|
| `ReuseOrCreate` | 复用或新建（默认）；可回池 |
| `ForceNew` | 总是新建；release 时关闭 |
| `NoPool` | 绕过池；不挤占活跃连接；release 时关闭 |

按请求设置：`khttp::RequestSetConnPolicy` / `KhHttpRequestSetConnectionPolicy`。

---

## English

Each `KhSession` owns a `FAST_MUTEX`-guarded fixed-capacity pool (default 8, max 1024, 2/host, 30 s idle). The reuse key matches scheme/host/port/family/TLS-versions/cert-policy+store/credential/full-`Policy`/SNI/**ALPN** plus proxy identity; an auto-ALPN relaxed comparator lets an empty-ALPN request reuse the connection whose negotiated ALPN was written back post-handshake. Acquire reuses a connected/idle/key-matching slot (idle-expired closed) and can share an active same-origin HTTP/2 connection while its stream lease count is below the negotiated/local limit; the **per-host quota key is coarser** (scheme+host+port+family only) and over-quota first tries to close an idle same-host connection else `STATUS_INSUFFICIENT_RESOURCES`. `NoPool` never evicts a live connection; `ForceNew`/`ReuseOrCreate` evict an idle slot. Return-to-pool requires success + reusable + `ReuseOrCreate`; **close-delimited and 101 are never pooled**; HTTP/1.0 needs explicit keep-alive; HTTP/2 reusability uses `IsReusable()` plus stream lease accounting so one pooled H2 connection can carry multiple active streams. Idle eviction is lazy (no timer). **Stale fresh-retry**: on failure of a reused connection under `ReuseOrCreate` with method GET/HEAD/OPTIONS and a connection-close/`STATUS_RETRY`/`STATUS_IO_TIMEOUT` status, it closes the connection and retries **exactly once** with `ForceNew`; POST/PUT/PATCH/DELETE are never replayed. Policies: `ReuseOrCreate` (default), `ForceNew`, `NoPool`.
