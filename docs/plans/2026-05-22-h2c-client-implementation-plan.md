# h2c Client Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking. Do not create git commits unless the user explicitly asks for commits.

**Goal:** Add HTTP/2 cleartext client support for both h2c prior knowledge and HTTP/1.1 Upgrade, with tests and samples.

**Architecture:** Refactor `Http2Connection` to use a small transport interface so the same HTTP/2 frame, HPACK, stream, and flow-control logic works over either TLS or raw WSK. Extend `Http2Client` with a transport mode, implement h2c prior knowledge by directly initializing HTTP/2 over TCP, and implement h2c Upgrade by performing an HTTP/1.1 101 handshake before HTTP/2 initialization.

**Tech Stack:** Windows kernel C++ driver code, WSK sockets, existing TLS/CNG stack, existing HTTP parser/request utilities, existing user-mode C++ tests.

---

## File Structure

- Modify: `src/KernelHttp/http2/Http2Connection.h`
  - Add a lightweight `Http2Transport` interface.
  - Add `Http2TlsTransport` and `Http2PlainTransport` adapters.
  - Add transport-based `Initialize`, `SendRequest`, and `Shutdown` overloads.
  - Keep existing TLS overloads as wrappers for compatibility.

- Modify: `src/KernelHttp/http2/Http2Connection.cpp`
  - Route internal `SendRaw`, `ReadExact`, `ReadFrame`, connection-frame handling, window updates, and shutdown through `Http2Transport`.
  - Keep protocol behavior unchanged for TLS.

- Modify: `src/KernelHttp/client/Http2Client.h`
  - Add `Http2TransportMode`.
  - Add `TransportMode` to `Http2RequestOptions`.

- Modify: `src/KernelHttp/client/Http2Client.cpp`
  - Split TLS and h2c connection setup.
  - Add h2c prior knowledge path.
  - Add h2c Upgrade path.
  - Add helper logic for HTTP2-Settings base64url encoding and HTTP/1.1 Upgrade request/response validation.
  - Build HTTP/2 pseudo headers with `:scheme` set to `http` for h2c and `https` for TLS.

- Modify: `src/KernelHttp/samples/Http2VerbSamples.h`
  - Add h2c sample result fields.

- Modify: `src/KernelHttp/samples/Http2VerbSamples.cpp`
  - Keep HTTPS h2 sample.
  - Add h2c prior knowledge sample.
  - Add h2c Upgrade sample.

- Modify: `tests/http2_frame_tests.cpp`
  - Add unit tests for h2c helper logic if helpers are placed in HTTP/2 layer.
  - Otherwise add a focused test source only if existing build practices require separation.

- Modify if needed: `src/KernelHttp/KernelHttp.vcxproj`
  - Only if new source/header files are created. Prefer no new files.

- Modify if needed: `src/KernelHttp/KernelHttp.vcxproj.filters`
  - Only if new source/header files are created. Prefer no new files.

---

## Chunk 1: Transport abstraction for HTTP/2 connection

### Task 1: Add transport interface and adapters

**Files:**
- Modify: `src/KernelHttp/http2/Http2Connection.h`
- Modify: `src/KernelHttp/http2/Http2Connection.cpp`

- [ ] **Step 1: Add `Http2Transport` interface to `Http2Connection.h`**

Add inside `namespace KernelHttp::http2`, before `Http2Connection`:

```cpp
class Http2Transport
{
public:
    virtual ~Http2Transport() noexcept = default;

    _Must_inspect_result_
    virtual NTSTATUS Send(
        _In_reads_bytes_(length) const UCHAR* data,
        SIZE_T length) noexcept = 0;

    _Must_inspect_result_
    virtual NTSTATUS Receive(
        _Out_writes_bytes_(length) UCHAR* data,
        SIZE_T length,
        _Out_ SIZE_T* bytesReceived) noexcept = 0;
};
```

- [ ] **Step 2: Add TLS and plain transport adapter declarations**

Add in `Http2Connection.h`:

```cpp
class Http2TlsTransport final : public Http2Transport
{
public:
    Http2TlsTransport(
        _Inout_ net::WskSocket& socket,
        _Inout_ tls::TlsConnection& tls) noexcept;

    _Must_inspect_result_
    NTSTATUS Send(
        _In_reads_bytes_(length) const UCHAR* data,
        SIZE_T length) noexcept override;

    _Must_inspect_result_
    NTSTATUS Receive(
        _Out_writes_bytes_(length) UCHAR* data,
        SIZE_T length,
        _Out_ SIZE_T* bytesReceived) noexcept override;

private:
    net::WskSocket& socket_;
    tls::TlsConnection& tls_;
};

class Http2PlainTransport final : public Http2Transport
{
public:
    explicit Http2PlainTransport(_Inout_ net::WskSocket& socket) noexcept;

    _Must_inspect_result_
    NTSTATUS Send(
        _In_reads_bytes_(length) const UCHAR* data,
        SIZE_T length) noexcept override;

    _Must_inspect_result_
    NTSTATUS Receive(
        _Out_writes_bytes_(length) UCHAR* data,
        SIZE_T length,
        _Out_ SIZE_T* bytesReceived) noexcept override;

private:
    net::WskSocket& socket_;
};
```

- [ ] **Step 3: Add transport-based `Http2Connection` overloads**

In `Http2Connection.h`, add public overloads:

```cpp
_Must_inspect_result_
NTSTATUS Initialize(_Inout_ Http2Transport& transport) noexcept;

_Must_inspect_result_
NTSTATUS SendRequest(
    _Inout_ Http2Transport& transport,
    _In_reads_(requestHeaderCount) const http::HttpHeader* requestHeaders,
    SIZE_T requestHeaderCount,
    _In_reads_bytes_opt_(bodyLength) const UCHAR* body,
    SIZE_T bodyLength,
    _Out_writes_(responseHeaderCapacity) http::HttpHeader* responseHeaders,
    SIZE_T responseHeaderCapacity,
    _Out_ SIZE_T* responseHeaderCount,
    _Out_writes_bytes_(responseBodyCapacity) char* responseBody,
    SIZE_T responseBodyCapacity,
    _Out_ SIZE_T* responseBodyLength,
    _Out_ USHORT* statusCode,
    _Out_writes_bytes_(nameValueCapacity) char* nameValueBuffer,
    SIZE_T nameValueCapacity) noexcept;

NTSTATUS Shutdown(_Inout_ Http2Transport& transport) noexcept;
```

- [ ] **Step 4: Change private helpers to accept `Http2Transport&`**

In `Http2Connection.h`, update private helper signatures:

```cpp
NTSTATUS SendRaw(
    _Inout_ Http2Transport& transport,
    _In_reads_bytes_(length) const UCHAR* data,
    SIZE_T length) noexcept;

NTSTATUS ReadExact(
    _Inout_ Http2Transport& transport,
    _Out_writes_bytes_(length) UCHAR* buffer,
    SIZE_T length) noexcept;

NTSTATUS ReadFrame(
    _Inout_ Http2Transport& transport,
    _Out_ Http2FrameHeader* header,
    _Out_writes_bytes_(payloadCapacity) UCHAR* payload,
    SIZE_T payloadCapacity,
    _Out_ SIZE_T* payloadLength) noexcept;

NTSTATUS HandleConnectionFrame(
    _Inout_ Http2Transport& transport,
    _In_ const Http2FrameHeader& header,
    _In_reads_bytes_(payloadLen) const UCHAR* payload,
    SIZE_T payloadLen) noexcept;

NTSTATUS SendWindowUpdateIfNeeded(
    _Inout_ Http2Transport& transport,
    ULONG streamId,
    ULONG consumed) noexcept;
```

- [ ] **Step 5: Implement transport adapters in `Http2Connection.cpp`**

Add after destructor or before `Http2Connection` methods:

```cpp
Http2TlsTransport::Http2TlsTransport(
    net::WskSocket& socket,
    tls::TlsConnection& tls) noexcept
    : socket_(socket), tls_(tls)
{
}

NTSTATUS Http2TlsTransport::Send(
    const UCHAR* data,
    SIZE_T length) noexcept
{
    return tls_.Send(socket_, data, length);
}

NTSTATUS Http2TlsTransport::Receive(
    UCHAR* data,
    SIZE_T length,
    SIZE_T* bytesReceived) noexcept
{
    return tls_.Receive(socket_, data, length, bytesReceived);
}

Http2PlainTransport::Http2PlainTransport(net::WskSocket& socket) noexcept
    : socket_(socket)
{
}

NTSTATUS Http2PlainTransport::Send(
    const UCHAR* data,
    SIZE_T length) noexcept
{
    SIZE_T sent = 0;
    NTSTATUS status = socket_.Send(data, length, &sent);
    if (NT_SUCCESS(status) && sent != length) {
        return STATUS_CONNECTION_DISCONNECTED;
    }
    return status;
}

NTSTATUS Http2PlainTransport::Receive(
    UCHAR* data,
    SIZE_T length,
    SIZE_T* bytesReceived) noexcept
{
    return socket_.Receive(data, length, bytesReceived);
}
```

If the exact `TlsConnection::Send/Receive` signatures differ, inspect `src/KernelHttp/tls/TlsConnection.h` and adapt this code without changing behavior.

- [ ] **Step 6: Convert `Http2Connection.cpp` internals to transport overloads**

Update implementations:

- `Initialize(net::WskSocket&, tls::TlsConnection&)` becomes a wrapper:

```cpp
NTSTATUS Http2Connection::Initialize(
    net::WskSocket& socket,
    tls::TlsConnection& tls) noexcept
{
    Http2TlsTransport transport(socket, tls);
    return Initialize(transport);
}
```

- Move existing body into:

```cpp
NTSTATUS Http2Connection::Initialize(Http2Transport& transport) noexcept
```

Replace all calls:
- `SendRaw(socket, tls, ...)` -> `SendRaw(transport, ...)`
- `ReadFrame(socket, tls, ...)` -> `ReadFrame(transport, ...)`

- `SendRequest(net::WskSocket&, tls::TlsConnection&, ...)` becomes a wrapper that constructs `Http2TlsTransport`.
- Move existing body into `SendRequest(Http2Transport&, ...)`.
- Replace all `ReadFrame`, `SendRaw`, `HandleConnectionFrame`, `SendWindowUpdateIfNeeded` calls to use `transport`.

- `Shutdown(net::WskSocket&, tls::TlsConnection&)` becomes a wrapper.
- Move existing body into `Shutdown(Http2Transport&)`.

- `SendRaw`, `ReadExact`, `ReadFrame`, `HandleConnectionFrame`, and `SendWindowUpdateIfNeeded` use only `Http2Transport&`.

- [ ] **Step 7: Build Debug to catch signature errors**

Run:

```powershell
pwsh -NoProfile -Command "msbuild KernelHttp.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- Build succeeds, or only pre-existing environment/toolchain errors appear.
- No C++ type errors from the transport refactor.

---

## Chunk 2: h2c mode API and request construction

### Task 2: Add transport mode and shared request header construction

**Files:**
- Modify: `src/KernelHttp/client/Http2Client.h`
- Modify: `src/KernelHttp/client/Http2Client.cpp`

- [ ] **Step 1: Add `Http2TransportMode`**

In `Http2Client.h`, add before `Http2RequestOptions`:

```cpp
enum class Http2TransportMode
{
    TlsAlpn,
    H2cPriorKnowledge,
    H2cUpgrade
};
```

Add to `Http2RequestOptions`:

```cpp
Http2TransportMode TransportMode = Http2TransportMode::TlsAlpn;
```

- [ ] **Step 2: Update option validation by mode**

In `Http2Client::SendRequest`, replace the current single validation block with validation that always requires:

- `options.RemoteAddress`
- non-empty `options.Path`
- response header/name-value/body buffers

For `TlsAlpn`, additionally require:

- `options.ServerName`
- `options.ServerNameLength > 0`
- `options.CertificateStore`

For h2c modes, require either:

- non-empty `options.Authority`, or
- non-null `options.ServerName` with `ServerNameLength > 0`

For `H2cUpgrade`, use the same authority requirement because the HTTP/1.1 `Host` header needs it.

- [ ] **Step 3: Factor request header construction into a helper**

Inside the anonymous namespace in `Http2Client.cpp`, add a helper:

```cpp
NTSTATUS BuildHttp2RequestHeaders(
    _In_ const Http2RequestOptions& options,
    _In_ http::HttpText scheme,
    _Out_writes_(MaxRequestHeaders) http::HttpHeader* requestHeaders,
    _Out_ SIZE_T* requestHeaderCount,
    _Out_writes_all_(MaxRequestHeaders) char lowerHeaderNames[MaxRequestHeaders][64],
    _Out_writes_(32) char* contentLengthBuffer) noexcept;
```

Move the existing pseudo-header/header-building logic into this helper.

Rules:
- `:scheme` value is the `scheme` parameter.
- `:authority` uses `options.Authority` if present; otherwise `{ options.ServerName, options.ServerNameLength }`.
- Keep forbidden HTTP/2 headers filtered.
- Keep lowercase extra header behavior.
- Return `STATUS_INVALID_PARAMETER` for invalid lowercasing or content-length formatting.
- Keep max header count limited by `MaxRequestHeaders`.

- [ ] **Step 4: Use helper in TLS path**

In the existing TLS path, call:

```cpp
status = BuildHttp2RequestHeaders(
    options,
    http::MakeText("https"),
    requestHeaders,
    &headerIdx,
    lowerHeaderNames,
    contentLengthBuffer);
```

Then call the existing `h2conn->SendRequest(...)`.

- [ ] **Step 5: Build Debug**

Run:

```powershell
pwsh -NoProfile -Command "msbuild KernelHttp.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- Existing TLS h2 path compiles.
- No behavior change yet except refactoring.

---

## Chunk 3: h2c prior knowledge

### Task 3: Implement direct cleartext HTTP/2 startup

**Files:**
- Modify: `src/KernelHttp/client/Http2Client.cpp`

- [ ] **Step 1: Split `SendRequest` into mode-specific branches**

In `Http2Client::SendRequest`, after validation and TCP connect:

```cpp
switch (options.TransportMode) {
case Http2TransportMode::TlsAlpn:
    return SendTlsHttp2Request(...);
case Http2TransportMode::H2cPriorKnowledge:
    return SendH2cPriorKnowledgeRequest(...);
case Http2TransportMode::H2cUpgrade:
    return SendH2cUpgradeRequest(...);
default:
    return STATUS_INVALID_PARAMETER;
}
```

Implement these as anonymous-namespace helper functions if no class state is needed. Keep them in `Http2Client.cpp`.

- [ ] **Step 2: Move existing TLS code into `SendTlsHttp2Request`**

Move the current TLS handshake, ALPN validation, HTTP/2 initialize, request send, shutdown, cleanup, and response population into a helper.

Keep behavior:
- ALPN must equal `h2`.
- Populate `response.NegotiatedAlpn`.
- Return `STATUS_NOT_SUPPORTED` if peer does not negotiate `h2`.

- [ ] **Step 3: Add `SendH2cPriorKnowledgeRequest`**

Implement:

```cpp
NTSTATUS SendH2cPriorKnowledgeRequest(
    _Inout_ net::WskSocket& socket,
    _In_ const Http2RequestOptions& options,
    _In_ const Http2ResponseBuffers& buffers,
    _Out_ Http2Response& response) noexcept
```

Behavior:
1. Allocate `http2::Http2Connection`.
2. Create `http2::Http2PlainTransport transport(socket)`.
3. Call `h2conn->Initialize(transport)`.
4. Build request headers with `scheme = "http"`.
5. Call `h2conn->SendRequest(transport, ...)`.
6. Populate response fields on success.
7. Call `h2conn->Shutdown(transport)`.
8. Delete connection.
9. Return status.

Do not set `NegotiatedAlpn` for h2c.

- [ ] **Step 4: Ensure socket cleanup is centralized**

`Http2Client::SendRequest` should:
- Connect socket once.
- Dispatch mode helper.
- Close socket once before returning.

TLS helper should delete `TlsConnection` and `Http2Connection`, but not close the socket directly if the caller closes it centrally.

- [ ] **Step 5: Build Debug**

Run:

```powershell
pwsh -NoProfile -Command "msbuild KernelHttp.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- h2c prior knowledge code compiles.
- Existing TLS path compiles.

---

## Chunk 4: h2c HTTP/1.1 Upgrade

### Task 4: Implement Upgrade request and response validation

**Files:**
- Modify: `src/KernelHttp/client/Http2Client.cpp`
- Modify if needed: `tests/http2_frame_tests.cpp`

- [ ] **Step 1: Add SETTINGS payload encoder for HTTP2-Settings**

Add anonymous-namespace helper:

```cpp
NTSTATUS EncodeHttp2SettingsPayload(
    _In_ const http2::Http2Settings& settings,
    _Out_writes_bytes_(capacity) UCHAR* output,
    SIZE_T capacity,
    _Out_ SIZE_T* outputLength) noexcept;
```

Behavior:
- Encode each setting as 6 bytes: 16-bit setting id, 32-bit value, network byte order.
- Include the same six settings as `Http2FrameCodec::EncodeSettings`:
  - `HeaderTableSize`
  - `EnablePush`
  - `MaxConcurrentStreams`
  - `InitialWindowSize`
  - `MaxFrameSize`
  - `MaxHeaderListSize`
- Do not include a frame header.

- [ ] **Step 2: Add base64url encoder without padding**

Add anonymous-namespace helper:

```cpp
NTSTATUS Base64UrlEncodeNoPadding(
    _In_reads_bytes_(inputLength) const UCHAR* input,
    SIZE_T inputLength,
    _Out_writes_(outputCapacity) char* output,
    SIZE_T outputCapacity,
    _Out_ SIZE_T* outputLength) noexcept;
```

Rules:
- Alphabet: `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_`
- No `=` padding.
- Return `STATUS_BUFFER_TOO_SMALL` if output capacity is insufficient.
- Return `STATUS_INVALID_PARAMETER` for null pointers.

- [ ] **Step 3: Add authority selection helper**

Add:

```cpp
bool GetAuthorityText(
    _In_ const Http2RequestOptions& options,
    _Out_ http::HttpText& authority) noexcept;
```

Rules:
- Prefer `options.Authority`.
- Otherwise use `options.ServerName` and `options.ServerNameLength`.
- Return false if neither is available.

- [ ] **Step 4: Add HTTP/1.1 Upgrade request builder**

Add:

```cpp
NTSTATUS BuildH2cUpgradeRequest(
    _In_ const Http2RequestOptions& options,
    _Out_writes_bytes_(capacity) char* output,
    SIZE_T capacity,
    _Out_ SIZE_T* outputLength) noexcept;
```

Request format:

```http
GET /path HTTP/1.1\r\n
Host: authority\r\n
Connection: Upgrade, HTTP2-Settings\r\n
Upgrade: h2c\r\n
HTTP2-Settings: encoded-settings\r\n
User-Agent: ...\r\n
\r\n
```

Rules:
- Method should use `HttpMethodToString(options.Method)`.
- Path uses `options.Path`.
- Host uses selected authority.
- Include `User-Agent` only if provided.
- Do not include request body in the HTTP/1.1 Upgrade request.
- The actual HTTP/2 request is sent after the `101` response.

- [ ] **Step 5: Add HTTP/1.1 Upgrade response reader**

Add:

```cpp
NTSTATUS ReadH2cUpgradeResponse(
    _Inout_ net::WskSocket& socket,
    _Out_writes_bytes_(bufferCapacity) char* buffer,
    SIZE_T bufferCapacity,
    _Out_ SIZE_T* responseLength) noexcept;
```

Behavior:
- Repeatedly receive until `\r\n\r\n` appears.
- Return `STATUS_BUFFER_TOO_SMALL` if headers exceed capacity.
- Return socket receive errors directly.

- [ ] **Step 6: Add Upgrade response validator**

Add:

```cpp
NTSTATUS ValidateH2cUpgradeResponse(
    _In_reads_bytes_(responseLength) const char* response,
    SIZE_T responseLength) noexcept;
```

Rules:
- Status line must start with `HTTP/1.1 101` or `HTTP/1.0 101`.
- Headers must include `Upgrade: h2c` case-insensitively.
- Headers must include `Connection` containing token `Upgrade` case-insensitively.
- Return `STATUS_NOT_SUPPORTED` if server does not upgrade.
- Return `STATUS_INVALID_NETWORK_RESPONSE` for malformed responses.

Use existing HTTP text comparison helpers if available; otherwise implement small local ASCII case-insensitive checks.

- [ ] **Step 7: Implement `SendH2cUpgradeRequest`**

Add helper:

```cpp
NTSTATUS SendH2cUpgradeRequest(
    _Inout_ net::WskSocket& socket,
    _In_ const Http2RequestOptions& options,
    _In_ const Http2ResponseBuffers& buffers,
    _Out_ Http2Response& response) noexcept
```

Behavior:
1. Build h2c Upgrade request into stack or heap buffer.
2. Send the full request via `socket.Send`.
3. Read HTTP/1.1 response headers.
4. Validate `101 Switching Protocols`.
5. Create `Http2PlainTransport`.
6. Create `Http2Connection`.
7. Call `Initialize(transport)`.
8. Build HTTP/2 request headers with scheme `http`.
9. Send actual HTTP/2 request and populate response.
10. Shutdown HTTP/2 connection and return status.

- [ ] **Step 8: Add unit tests for base64url and request builder**

In `tests/http2_frame_tests.cpp`, add tests near existing HTTP/2 protocol tests.

Test examples:
- Base64url output has no `=`.
- Known bytes `{ 0x00, 0x01, 0x02 }` encode to `AAEC`.
- Upgrade request contains:
  - request line
  - `Host:`
  - `Connection: Upgrade, HTTP2-Settings`
  - `Upgrade: h2c`
  - `HTTP2-Settings:`
- Validation accepts a valid `101` response.
- Validation rejects `200 OK`.

If helpers remain internal to `Http2Client.cpp`, expose only minimal testable helpers in a header is not preferred. Instead, add tests at the lowest available layer only if code organization supports it. Do not over-expose production internals solely for tests.

- [ ] **Step 9: Run tests**

Run existing test binaries if present:

```powershell
pwsh -NoProfile -Command ".\tests\http2_frame_tests.exe; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }"
```

If the binary is stale or missing required new tests, build the test according to existing local toolchain conventions. If no test build script exists, document the exact command attempted and the toolchain error.

- [ ] **Step 10: Build Debug after tests**

Run:

```powershell
pwsh -NoProfile -Command "msbuild KernelHttp.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- Driver Debug build succeeds or reports only environment/toolchain issues unrelated to code changes.

---

## Chunk 5: Samples

### Task 5: Add h2c samples

**Files:**
- Modify: `src/KernelHttp/samples/Http2VerbSamples.h`
- Modify: `src/KernelHttp/samples/Http2VerbSamples.cpp`

- [ ] **Step 1: Extend sample result struct**

In `Http2VerbSampleResults`, add:

```cpp
Http2VerbSampleResult H2cPriorKnowledgeGet = {};
Http2VerbSampleResult H2cUpgradeGet = {};
```

- [ ] **Step 2: Generalize sample request helper**

In `Http2VerbSamples.cpp`, update `SendHttp2SampleRequest` or add a parallel helper to accept:

- sample name
- server name as wide string
- service name as wide string
- authority as `HttpText`
- TLS server name as char pointer for TLS mode
- transport mode
- method/path/body/content-type

Rules:
- For `TlsAlpn`, initialize pinned certificate store as today.
- For h2c modes, do not initialize certificate store and do not set `CertificateStore`.
- Set `options.TransportMode`.
- Set `options.Authority`.
- Set `options.ServerName` and `ServerNameLength` only when needed by mode/default authority.

- [ ] **Step 3: Keep existing HTTPS h2 samples unchanged in behavior**

Existing samples should still call:
- `TransportMode = Http2TransportMode::TlsAlpn`
- `ServerName = "nghttp2.org"`
- `CertificateStore = &certificateStore`

- [ ] **Step 4: Add h2c prior knowledge sample**

Use an h2c-capable endpoint configured as constants in the sample file.

Add constants:

```cpp
constexpr const wchar_t* H2cServerName = L"localhost";
constexpr const wchar_t* H2cServiceName = L"8080";
constexpr const char* H2cAuthority = "localhost:8080";
constexpr SIZE_T H2cAuthorityLength = sizeof("localhost:8080") - 1;
```

Use:
- `TransportMode = Http2TransportMode::H2cPriorKnowledge`
- path `/`
- method GET

Do not hardcode an external generated URL. Prefer local endpoint constants so users can point a local h2c server at the sample.

- [ ] **Step 5: Add h2c Upgrade sample**

Use the same local constants:
- `TransportMode = Http2TransportMode::H2cUpgrade`
- path `/`
- method GET

- [ ] **Step 6: Update logging**

For all HTTP/2 samples, log transport mode:
- `TLS h2`
- `h2c prior knowledge`
- `h2c upgrade`

For h2c, ALPN output should be empty.

- [ ] **Step 7: Build Debug**

Run:

```powershell
pwsh -NoProfile -Command "msbuild KernelHttp.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- Samples compile in driver build.

---

## Chunk 6: Final verification

### Task 6: Run tests and Debug build

**Files:**
- No new source edits unless verification finds defects.

- [ ] **Step 1: Run HTTP/2 frame tests**

```powershell
pwsh -NoProfile -Command ".\tests\http2_frame_tests.exe"
```

Expected:
- Exit code 0.

- [ ] **Step 2: Run HPACK tests**

```powershell
pwsh -NoProfile -Command ".\tests\hpack_tests.exe"
```

Expected:
- Exit code 0.

- [ ] **Step 3: Run available TLS record tests if binary exists**

```powershell
pwsh -NoProfile -Command "if (Test-Path .\tests\tls_record_tests.exe) { .\tests\tls_record_tests.exe; exit $LASTEXITCODE }"
```

Expected:
- Exit code 0 if binary exists.

- [ ] **Step 4: Run Debug build after tests**

Project instruction requires a Debug build after tests.

```powershell
pwsh -NoProfile -Command "msbuild KernelHttp.sln /p:Configuration=Debug /p:Platform=x64"
```

Expected:
- Build succeeds.
- If toolchain is unavailable, capture and report the exact environment error.

- [ ] **Step 5: Inspect git diff**

```powershell
pwsh -NoProfile -Command "git diff -- src/KernelHttp/http2/Http2Connection.h src/KernelHttp/http2/Http2Connection.cpp src/KernelHttp/client/Http2Client.h src/KernelHttp/client/Http2Client.cpp src/KernelHttp/samples/Http2VerbSamples.h src/KernelHttp/samples/Http2VerbSamples.cpp tests/http2_frame_tests.cpp docs/plans/2026-05-22-h2c-client-design.md"
```

Check:
- No accidental unrelated edits.
- No generated binaries committed.
- No fallback/downgrade behavior was added.
- No WinHTTP/WinINet/SChannel path was introduced.
- h2c has both prior knowledge and Upgrade paths.
- TLS h2 ALPN path remains intact.

- [ ] **Step 6: Report results**

Report:
- Files changed.
- Tests run and outcomes.
- Debug build outcome.
- Any toolchain/environment limitations.
- Do not commit unless explicitly requested by the user.