# 高层 API / High-Level API

HTTP 在命名空间 `khttp`，WebSocket 在 `kws`。高层 HTTP API 隐藏 WSK 运行时：调用方创建 `Session` 后即可发送请求，不再向高层 `SessionCreate` 传入 `net::WskClient`。

[English](#english) | 简体中文

---

## 简体中文

### 生命周期与所有权

所有高层公开对象都必须通过创建函数获得，并用匹配的 `Close` / `Release` 释放。不要在内核调用方栈上声明 `Session`、`Request`、`Response`、`AsyncOp`、`Headers`、`Body`、`SendOptions` 或 `AsyncOptions`。

```cpp
NTSTATUS SessionCreate(Session** session) noexcept;
NTSTATUS SessionCreate(const SessionConfig* config, Session** session) noexcept;
void     SessionClose(Session* session) noexcept;

NTSTATUS RequestCreate(Session* session, Request** request) noexcept;
void     RequestRelease(Request* request) noexcept;

void     ResponseRelease(Response* response) noexcept;
void     AsyncRelease(AsyncOp* operation) noexcept;
void     Destroy() noexcept;
```

`Session` 内部拥有 WSK runtime 和 engine session。`Request` 是绑定到 `Session` 的发送句柄，不保存 URL、method、header、body 或 TLS 设置。`Response` 与发起它的句柄独立，取完后用 `ResponseRelease`。使用过异步 API 后，驱动卸载前必须调用 `khttp::Destroy()` 等待在飞异步操作完成；同步-only 路径可以无条件调用。

所有高层 HTTP/WS 调用要求 `PASSIVE_LEVEL`。

### Send 模型

`Session*` 和 `Request*` 是等价的 send handle。发送调用中，send handle、`Method`、URL、结果输出指针是必填；`Headers`、`Body`、`SendOptions` / `AsyncOptions` 可为 `nullptr`。

```cpp
NTSTATUS Send(Session* session, Method method, const char* url,
              const Headers* headers, const Body* body,
              const SendOptions* options, Response** response) noexcept;
NTSTATUS SendEx(Session* session, Method method, const char* url, SIZE_T urlLength,
                const Headers* headers, const Body* body,
                const SendOptions* options, Response** response) noexcept;

NTSTATUS Send(Request* request, Method method, const char* url,
              const Headers* headers, const Body* body,
              const SendOptions* options, Response** response) noexcept;
NTSTATUS SendEx(Request* request, Method method, const char* url, SIZE_T urlLength,
                const Headers* headers, const Body* body,
                const SendOptions* options, Response** response) noexcept;
```

非 `Ex` 版本按 NUL 结尾字符串计算 URL 长度。动词快捷函数最终都转到 `SendEx`：`Get/GetEx`、`Post/PostEx`、`Put/PutEx`、`Patch/PatchEx`、`Delete/DeleteEx`、`Head/HeadEx`、`Options/OptionsEx`。`Post`、`Put`、`Patch` 的 `body == nullptr` 表示发送空 body。

库会合成协议必需 header，例如 `Host`、`Content-Length` / framing。调用方 header 会按大小写不敏感字段名覆盖可覆盖默认值；库受控 header 仍会拒绝或由库合成。

### Headers

`Headers` 是堆句柄，添加时总是复制 name/value。添加后调用方可立即修改或释放源缓冲。

```cpp
NTSTATUS HeadersCreate(Headers** headers) noexcept;
NTSTATUS HeadersAdd(Headers* headers, const char* name, const char* value) noexcept;
NTSTATUS HeadersAddEx(Headers* headers, const char* name, SIZE_T nameLength,
                      const char* value, SIZE_T valueLength) noexcept;
void     HeadersRelease(Headers* headers) noexcept;
```

header name 必须是合法 token；name/value 禁止 CR/LF 注入。`Host`、`Content-Length`、连接 framing 相关字段等库受控 header 不允许由调用方添加。

### Body

`Body` 是堆句柄。无 `Copy` 的 bytes/text/json helper 引用调用方内存：同步发送返回前必须保持有效；异步发送完成或取消前必须保持有效。`Copy` 版本在创建时复制到堆。

```cpp
NTSTATUS BodyCreateBytes(const UCHAR* data, SIZE_T dataLength, Body** body) noexcept;
NTSTATUS BodyCreateBytesCopy(const UCHAR* data, SIZE_T dataLength, Body** body) noexcept;
NTSTATUS BodyCreateText(const char* text, SIZE_T textLength, const char* contentType, Body** body) noexcept;
NTSTATUS BodyCreateTextCopy(const char* text, SIZE_T textLength, const char* contentType, Body** body) noexcept;
NTSTATUS BodyCreateJson(const char* json, SIZE_T jsonLength, Body** body) noexcept;
NTSTATUS BodyCreateJsonCopy(const char* json, SIZE_T jsonLength, Body** body) noexcept;
NTSTATUS BodyCreateForm(const NameValuePair* pairs, SIZE_T pairCount, Body** body) noexcept;
NTSTATUS BodyCreateMultipart(const MultipartPart* parts, SIZE_T partCount, Body** body) noexcept;
NTSTATUS BodyCreateFile(const char* filePath, const char* contentType, Body** body) noexcept;
NTSTATUS BodySetMode(Body* body, RequestBodyMode mode) noexcept;
NTSTATUS BodyAddTrailer(Body* body, const char* name, const char* value) noexcept;
void     BodyRelease(Body* body) noexcept;
```

所有带长度的 `*Ex` 版本也可用。`BodyCreateJson*` 只设置 `Content-Type: application/json; charset=utf-8` 并透传字节，不解析、不校验、不构造 JSON。`BodyAddTrailer*` 只适用于 `RequestBodyMode::Chunked`，并继续拒绝库受控或敏感 trailer 字段。

### Options

`SendOptions` 和 `AsyncOptions` 必须通过创建函数获得，字段可通过指针修改。

```cpp
NTSTATUS SendOptionsCreate(SendOptions** options) noexcept;
void     SendOptionsRelease(SendOptions* options) noexcept;

NTSTATUS AsyncOptionsCreate(AsyncOptions** options) noexcept;
void     AsyncOptionsRelease(AsyncOptions* options) noexcept;
```

`SendOptions` 常用字段：

| 字段 | 默认 | 说明 |
|------|------|------|
| `MaxResponseBytes` | `0` | 0 表示不设置调用方响应体聚合上限；非零表示主动限制 |
| `Flags` | `SendFlagNone` | `SendFlagAggregateWithCallbacks`、`SendFlagDisableAutoRedirect` |
| `MaxRedirects` | `10` | 自动重定向上限 |
| `OnHeader` / `OnBody` | `nullptr` | 响应头/响应体分块回调 |
| `CallbackContext` | `nullptr` | 回调上下文 |
| `Tls` / `HasTlsOverride` | 默认 TLS / `false` | per-call TLS 覆盖 |
| `ConnectionPolicy` | `ReuseOrCreate` | 连接复用策略 |
| `Family` | `Any` | 地址族选择 |

`AsyncOptions` 包含 `SendOptions Send` 以及 `OnComplete` / `CompletionContext`。

### 异步

异步入口统一使用 `Async` 前缀。由于 C++ 中 `AsyncOptions` 类型名与 `AsyncOptions()` 函数名冲突，HTTP OPTIONS 动词使用 `AsyncOptionsRequest` / `AsyncOptionsRequestEx`。

```cpp
NTSTATUS AsyncSendEx(Session* session, Method method, const char* url, SIZE_T urlLength,
                     const Headers* headers, const Body* body,
                     const AsyncOptions* options, AsyncOp** operation) noexcept;
NTSTATUS AsyncSendEx(Request* request, Method method, const char* url, SIZE_T urlLength,
                     const Headers* headers, const Body* body,
                     const AsyncOptions* options, AsyncOp** operation) noexcept;

NTSTATUS AsyncGetEx(...);
NTSTATUS AsyncPostEx(...);
NTSTATUS AsyncPutEx(...);
NTSTATUS AsyncPatchEx(...);
NTSTATUS AsyncDeleteEx(...);
NTSTATUS AsyncHeadEx(...);
NTSTATUS AsyncOptionsRequestEx(...);

NTSTATUS AsyncWait(AsyncOp* operation, ULONG timeoutMs) noexcept;
NTSTATUS AsyncCancel(AsyncOp* operation) noexcept;
NTSTATUS AsyncGetStatus(const AsyncOp* operation) noexcept;
bool     AsyncIsCompleted(const AsyncOp* operation) noexcept;
bool     AsyncIsCanceled(const AsyncOp* operation) noexcept;
NTSTATUS AsyncGetResponse(AsyncOp* operation, Response** response) noexcept;
```

取出响应后仍需 `ResponseRelease`，最后 `AsyncRelease`。取消是协作式的：请求取消后仍应等待终态。

### Response

```cpp
ULONG        ResponseStatusCode(const Response* response) noexcept;
const UCHAR* ResponseBody(const Response* response) noexcept;
SIZE_T       ResponseBodyLength(const Response* response) noexcept;
SIZE_T       ResponseHeaderCount(const Response* response) noexcept;
NTSTATUS     ResponseGetHeader(const Response* response, const char* name, SIZE_T nameLength,
                               const char** value, SIZE_T* valueLength) noexcept;
void         ResponseRelease(Response* response) noexcept;
```

`Response` 内存由库拥有，指针在 `ResponseRelease` 前有效。

### 示例

```cpp
khttp::Session* session = nullptr;
NTSTATUS status = khttp::SessionCreate(&session);
if (!NT_SUCCESS(status)) {
    return status;
}

khttp::Headers* headers = nullptr;
khttp::Body* body = nullptr;
khttp::SendOptions* options = nullptr;
khttp::Response* response = nullptr;

status = khttp::HeadersCreate(&headers);
if (NT_SUCCESS(status)) {
    status = khttp::HeadersAdd(headers, "User-Agent", "KernelHttp/1.0");
}
if (NT_SUCCESS(status)) {
    status = khttp::BodyCreateJson("{\"hello\":\"world\"}", 17, &body);
}
if (NT_SUCCESS(status)) {
    status = khttp::SendOptionsCreate(&options);
}
if (NT_SUCCESS(status)) {
    options->MaxResponseBytes = 0;
    status = khttp::PostEx(session, "https://api.example.com/v1", 26,
                           headers, body, options, &response);
}

khttp::ResponseRelease(response);
khttp::SendOptionsRelease(options);
khttp::BodyRelease(body);
khttp::HeadersRelease(headers);
khttp::SessionClose(session);
```

### WebSocket

`kws` 仍使用 `khttp::Session*`。`ConnectConfig.Headers` 可传 opening-handshake 额外头；库受控头（`Host`、`Connection`、`Upgrade`、`Sec-WebSocket-*` 等）会被拒绝。`ConnectConfig.AllowWebSocketOverHttp2=true` 时，`wss` 可显式 opt-in RFC 8441；默认仍走 HTTP/1.1 Upgrade。

---

## English

The high-level HTTP API hides WSK. Create a `khttp::Session` with `SessionCreate(&session)` or `SessionCreate(&config, &session)`; do not pass `net::WskClient` to high-level session creation.

All public high-level objects are heap handles: create `Session`, `Request`, `Headers`, `Body`, `SendOptions`, `AsyncOptions`, `AsyncOp`, and `Response` through API functions and release them with the matching close/release function. Do not stack-allocate these objects in kernel callers.

`Session*` and `Request*` are equivalent send handles. `Request` is only a session-bound send handle; URL, method, headers, body, TLS override, connection policy, and address family are passed per call:

```cpp
SendEx(handle, method, url, urlLength, headers, body, options, &response);
AsyncSendEx(handle, method, url, urlLength, headers, body, asyncOptions, &operation);
```

`headers`, `body`, and options may be `nullptr`; the handle, method, URL, and output pointer are required. Headers copy name/value data. Non-copy body helpers reference caller memory until sync send returns or async send completes/cancels; copy helpers own heap copies. JSON helpers only set `application/json; charset=utf-8` and pass bytes through.

Async entry points use the `Async` prefix: `AsyncGet`, `AsyncPost`, `AsyncSend`, `AsyncWait`, `AsyncCancel`, `AsyncGetResponse`, `AsyncRelease`. The HTTP OPTIONS helper is named `AsyncOptionsRequest` / `AsyncOptionsRequestEx` to avoid a C++ name collision with the `AsyncOptions` type. After using async APIs, call `khttp::Destroy()` before driver unload.
