# HTTP/2 与 HPACK / HTTP/2 & HPACK

命名空间 `KernelHttp::http2`。RFC 9113 + HPACK RFC 7541，客户端单流串行。内容依据 `src/KernelHttpLib/http2/` 实现。

[English](#english) | 简体中文

---

## 简体中文

### 帧与常量

```cpp
enum class Http2FrameType : UCHAR {
    Data=0x0, Headers=0x1, Priority=0x2, RstStream=0x3, Settings=0x4,
    PushPromise=0x5, Ping=0x6, GoAway=0x7, WindowUpdate=0x8, Continuation=0x9
};
```
帧头 9 字节、默认最大帧 16384、最大允许帧 2^24-1、初始窗口 65535、最大窗口 0x7fffffff、前导 `"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"`（24 字节）。

### 连接建立与 SETTINGS

- 发前导 + 客户端 6 项 SETTINGS（`EnablePush=0`、`MaxConcurrentStreams=100`、`InitialWindowSize=65535`、`MaxFrameSize=32768`、`HeaderTableSize=4096`、`MaxHeaderListSize=头块容量`）。
- **立即发 ACK，不阻塞等服务端 ACK**（服务端常延迟到有请求才 ACK）。
- 校验对端 SETTINGS：payload 须 6 的倍数（否则 `FRAME_SIZE_ERROR`）、`ENABLE_PUSH != 0` → `PROTOCOL_ERROR`、`InitialWindowSize > 0x7fffffff` → `FLOW_CONTROL_ERROR`、`MaxFrameSize` 越界 → `PROTOCOL_ERROR`。
- 首帧必须是 stream 0 上的非 ACK SETTINGS，否则 GOAWAY。
- **SETTINGS 超时**：无独立计时器——后续读到 `STATUS_IO_TIMEOUT` 且尚未收到对端 ACK → GOAWAY `SETTINGS_TIMEOUT`。

### HEADERS / CONTINUATION

- 累计头块超头块容量（默认 32 KiB，最大 256 KiB）→ GOAWAY `ENHANCE_YOUR_CALM` + `STATUS_BUFFER_TOO_SMALL`。
- CONTINUATION 序列严格校验；**洪泛防护**：`Http2MaxContinuationFrames=64`、`Http2MaxEmptyContinuationFrames=4`，超限 GOAWAY `PROTOCOL_ERROR`（CVE-2024-27316 类）。
- HPACK 解码失败映射：`STATUS_BUFFER_TOO_SMALL`→`ENHANCE_YOUR_CALM`；压缩失败→`COMPRESSION_ERROR`。

### 头校验

- 字段名拒绝大写、控制字符、空格、内嵌 `:`；值拒绝 `\0\r\n`。
- 伪头仅 `:status`，须先于普通头、不重复、trailer 中不得出现；缺 `:status`（非 trailer）→ 非法。
- 连接专属头禁止：`connection`/`keep-alive`/`proxy-connection`/`transfer-encoding`/`upgrade`；`te` 仅允许值 `trailers`。
- 1xx interim：`:status 101` 或 interim+END_STREAM → RST_STREAM `PROTOCOL_ERROR`；其余 interim 重置块续读最终响应。

### 流控

- DATA 在 headers 前、或 body 被禁（HEAD/1xx/204/304）→ RST_STREAM `PROTOCOL_ERROR`。
- 连接级窗口越界 → GOAWAY `FLOW_CONTROL_ERROR`；**WINDOW_UPDATE 阈值 = 初始窗口一半（32767）**，达阈值补发。
- 出站请求体受 `min(连接发送窗口, 流远端窗口)` 限制，窗口耗尽则刷缓冲并处理对端 WINDOW_UPDATE。

### GOAWAY / RST_STREAM / PUSH_PROMISE

- GOAWAY：错误码非 0 → `STATUS_CONNECTION_DISCONNECTED`；错误码 0 但活动流 id > lastStreamId → **`STATUS_RETRY`**（该流未处理，可重试）。
- RST_STREAM：`NoError` 且已收终态响应 → 成功；否则 `STATUS_CONNECTION_DISCONNECTED`。
- 客户端 `EnablePush=0`，**任何 PUSH_PROMISE → GOAWAY `PROTOCOL_ERROR`**；错位控制帧亦然。

### h2c 模式

- **prior knowledge**：直接 `Initialize` + `SendRequest`。
- **Upgrade**：`InitializeAfterUpgrade` 跑完前导/SETTINGS 后置 `nextStreamId_=3`（保留 stream 1 给升级请求），用 `ReceiveResponse(streamId=1)`；`EncodeSettingsPayloadBase64Url` 产出 `HTTP2-Settings` 值。客户端 `Http2Client` 的 Upgrade 模式**禁止请求体**并重放 101 后残留字节。

### HPACK

- 整数：续字节 ≤5、移位/累加溢出 → `STATUS_INTEGER_OVERFLOW`。
- Huffman：>30 bit 码 / EOS / 非全 1 padding → `STATUS_INVALID_NETWORK_RESPONSE`。
- 动态表：大小更新仅限块首且 ≤协商 `HeaderTableSize`；表项大于 max 清空全表；FIFO 驱逐。
- header-list 大小按 `name+value+32`（`HpackEntryOverhead`）计，超 `MaxHeaderListSize` → 非法。
- 静态表 61 项；**编码端对 `authorization`/`cookie`/`proxy-authorization` 强制 Never-Indexed**，不入动态表。

### 边界

单流串行、无多路复用、**不复用 h2 连接**（每请求新建 + GOAWAY）、不发 PRIORITY/主动 PING、不支持 server push 与 RFC 8441；高层 khttp 不暴露 h2c（仅 `Http2Client`）。

---

## English

Namespace `KernelHttp::http2`, RFC 9113 + HPACK 7541, single-stream serial, grounded in `src/KernelHttpLib/http2/`.

Connection: preface + 6 client SETTINGS, **ACK sent immediately and not awaited**; peer SETTINGS validated (multiple-of-6 payload, ENABLE_PUSH!=0 → PROTOCOL_ERROR, window/frame bounds); first frame must be a non-ACK SETTINGS on stream 0. **SETTINGS timeout** is surfaced when a later read times out before the peer ACK → GOAWAY SETTINGS_TIMEOUT. HEADERS/CONTINUATION with **flood guards (64 / 4 empty)** and header-block cap (32 KiB default, 256 KiB max → ENHANCE_YOUR_CALM). Header validation: only `:status` pseudo-header, must precede regular headers; connection-specific headers forbidden; `te` only `trailers`; 1xx interim handled (101/interim+END_STREAM rejected). Flow control with half-initial-window WINDOW_UPDATE threshold (32767). GOAWAY: non-zero code → `STATUS_CONNECTION_DISCONNECTED`; clean GOAWAY with active stream beyond lastStreamId → **`STATUS_RETRY`**. PUSH_PROMISE always → GOAWAY PROTOCOL_ERROR. h2c prior-knowledge vs upgrade (upgrade reserves stream 1, replays post-101 bytes, forbids a request body). HPACK: continuation-byte ≤5, Huffman rejects >30-bit/EOS/bad padding, table-size update only at block start, header-list size = name+value+32, **never-indexed forced for authorization/cookie/proxy-authorization**, static table 61 entries. Boundaries: single-stream, no multiplexing, no h2 connection reuse, no PRIORITY/proactive PING/push/RFC 8441; h2c only via `Http2Client`.
