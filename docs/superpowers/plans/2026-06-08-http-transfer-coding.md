# HTTP Transfer Coding Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement HTTP/1.1 response `Transfer-Encoding` chains beyond plain `chunked`, including `gzip`, `deflate`, and `compress`, while enforcing RFC 9112 framing rules.

**Architecture:** Split message transfer-coding from representation content-coding. Add a small `HttpTransferCoding` layer that parses `Transfer-Encoding` in order, decodes the wire body in reverse order, and reuses shared bounded compression decoders instead of duplicating gzip/deflate code. Keep request bodies on the existing `Content-Length` path for now, but reject user-supplied `Transfer-Encoding` on requests until chunked upload is deliberately implemented.

**Tech Stack:** Windows kernel C++ under `/kernel`, nonpaged pool allocation through existing global `new/delete`, current gzip/deflate/br content decoding code, new bounded LZW `compress` decoder, existing HTTP parser/user-mode tests, MSBuild Debug x64. All commands use `pwsh`; do not run the forbidden `tests/integration/https_smoke.ps1 -Configuration Debug -Platform x64 -SkipDriverBuild` command.

---

## Scope

Implement response-side transfer-coding support for:

- `chunked`
- `gzip`
- `deflate`
- `compress`

Keep these explicit boundaries:

- `br` remains a `Content-Encoding` only unless the HTTP transfer-coding registry is updated and we intentionally add it later.
- Deprecated aliases `x-gzip` and `x-compress` are accepted as RFC 9112 aliases for `gzip` and `compress`.
- Unknown transfer codings return `STATUS_NOT_SUPPORTED`.
- `Transfer-Encoding` with `Content-Length` is rejected as `STATUS_INVALID_NETWORK_RESPONSE`.
- `Transfer-Encoding` on an HTTP/1.0 response is rejected as a framing error.
- Known transfer codings with transfer parameters, such as `gzip;foo=bar`, are rejected as `STATUS_INVALID_NETWORK_RESPONSE`.
- Empty comma-list elements are ignored when at least one effective coding remains; a `Transfer-Encoding` field with no effective coding is rejected.
- Request upload still uses `Content-Length`; user-supplied request `Transfer-Encoding` is rejected before request bytes are built.
- Response trailers continue to be consumed but not exposed to callers in this plan.

## File Structure

- Create: `include/KernelHttp/http/HttpCoding.h`
  - Shared coding enum and decode entry points for gzip, deflate, brotli content coding, and compress transfer coding.
- Create: `src/KernelHttpLib/http/HttpCoding.cpp`
  - Move bounded gzip/deflate/brotli helpers out of `HttpContentEncoding.cpp`.
  - Add bounded `compress` LZW decoder.
- Create: `include/KernelHttp/http/HttpTransferCoding.h`
  - Transfer-coding parse/decode API used by `HttpParser`.
- Create: `src/KernelHttpLib/http/HttpTransferCoding.cpp`
  - Parse `Transfer-Encoding`, validate chain rules, decode in reverse order, and compute wire bytes consumed.
- Modify: `src/KernelHttpLib/http/HttpContentEncoding.cpp`
  - Remove duplicated compression internals and call `HttpCoding`.
- Modify: `include/KernelHttp/http/HttpContentEncoding.h`
  - No public behavior change expected; include only if helper declarations require it.
- Modify: `include/KernelHttp/http/HttpResponse.h`
  - Replace `HasChunkedTransferEncoding()` with a stricter helper or keep it only for compatibility and stop using it for parser framing.
- Modify: `src/KernelHttpLib/http/HttpResponse.cpp`
  - Add or adjust header value helpers used by the transfer-coding parser.
- Modify: `src/KernelHttpLib/http/HttpParser.cpp`
  - Route transfer-coded responses through `HttpTransferCoding`.
  - Reject `Transfer-Encoding` plus `Content-Length`.
  - Treat final non-`chunked` response coding as close-delimited.
  - Add `205 Reset Content` to no-body status handling while this parser area is touched.
- Modify: `src/KernelHttpLib/http/HttpRequest.cpp`
  - Reject request options containing a `Transfer-Encoding` header until upload transfer-coding is implemented.
- Modify: `src/KernelHttpLib/engine/HttpEngine.cpp`
  - Filter/reject `Transfer-Encoding` in `BuildHttpRequestOptions` instead of forwarding it to the request builder.
- Modify: `src/KernelHttpLib/client/HttpClient.cpp`
  - Preserve parser behavior for close-delimited transfer-coded responses.
- Modify: `src/KernelHttpLib/client/HttpsClient.cpp`
  - Preserve parser behavior for close-delimited transfer-coded responses.
- Modify: `src/KernelHttpLib/KernelHttpLib.vcxproj`
  - Add new `.cpp` and `.h` files.
- Modify: `src/KernelHttpLib/KernelHttpLib.vcxproj.filters`
  - Add new files to the HTTP filter.
- Modify: `tests/http_parser_tests.cpp`
  - Add failing tests for response transfer-coding chains and parser framing.
- Modify: `tests/khttp_tests.cpp`
  - Add request-side API test rejecting `Transfer-Encoding`.
- Modify: `docs/api-overview.md`
  - Update capability table: response transfer-coding supports gzip/deflate/compress/chunked chains; request chunked upload remains unsupported.
- Modify: `docs/high-level-api.md`
  - Document request `Transfer-Encoding` rejection and response transfer-coding behavior.
- Modify: `README.md`
  - Update HTTP/1.1 capability wording.
- Modify: `README_en.md`
  - Mirror the README wording in English.

## Chunk 1: Response Transfer-Coding Tests

### Task 1: Add failing tests for supported transfer-coding chains

**Files:**
- Modify: `tests/http_parser_tests.cpp`

- [ ] **Step 1: Add helpers for transfer-coded fixtures**

Add helpers near the existing response fixture builders:

```cpp
bool BuildChunkedBody(
    const unsigned char* body,
    size_t bodyLength,
    char* destination,
    size_t destinationCapacity,
    size_t* destinationLength);

bool BuildTransferEncodedResponse(
    const char* transferEncoding,
    const unsigned char* wireBody,
    size_t wireBodyLength,
    char* response,
    size_t responseCapacity,
    size_t* responseLength);
```

These helpers must build exact HTTP response bytes and must not depend on network I/O.

- [ ] **Step 2: Add `gzip, chunked` response test**

Use the existing `GzipBody` fixture:

```cpp
void TestTransferEncodingGzipChunked()
{
    char chunked[256] = {};
    size_t chunkedLength = 0;
    Expect(BuildChunkedBody(GzipBody, sizeof(GzipBody), chunked, sizeof(chunked), &chunkedLength),
        "gzip transfer body is chunked");

    char responseBytes[384] = {};
    size_t responseLength = 0;
    Expect(BuildTransferEncodedResponse("gzip, chunked",
        reinterpret_cast<const unsigned char*>(chunked),
        chunkedLength,
        responseBytes,
        sizeof(responseBytes),
        &responseLength),
        "gzip chunked transfer response builds");

    HttpHeader headers[8] = {};
    char decoded[64] = {};
    char scratch[128] = {};
    HttpParseOptions options = {};
    options.Headers = headers;
    options.HeaderCapacity = 8;
    options.DecodedBody = decoded;
    options.DecodedBodyCapacity = sizeof(decoded);
    options.ScratchBody = scratch;
    options.ScratchBodyCapacity = sizeof(scratch);

    HttpResponse response = {};
    const NTSTATUS status = HttpParser::ParseResponse(responseBytes, responseLength, options, response);

    Expect(status == STATUS_SUCCESS, "gzip, chunked transfer coding parses");
    Expect(MemoryEqualsLiteral(response.Body, response.BodyLength, EncodedBodyLiteral),
        "gzip transfer coding is decoded after chunked framing");
}
```

- [ ] **Step 3: Add `deflate, chunked` response test**

Use existing `DeflateZlibBody` and expect `EncodedBodyLiteral`.

- [ ] **Step 4: Add `compress, chunked` response test**

Add a small static UNIX compress/LZW fixture for a short literal such as `"compress body"`. Keep the fixture under 128 bytes and include a comment with the exact uncompressed literal and expected decoded length.

- [ ] **Step 5: Add final non-chunked response transfer-coding test**

Add `Transfer-Encoding: gzip` with no `Content-Length`. First parse without `MessageCompleteOnConnectionClose` and expect `STATUS_MORE_PROCESSING_REQUIRED`; then set `MessageCompleteOnConnectionClose = true` and expect decoded body success.

- [ ] **Step 6: Add reverse-chain test for close-delimited final coding**

Add `Transfer-Encoding: chunked, gzip` where the whole gzip payload contains a chunked stream. Require `MessageCompleteOnConnectionClose = true`, decode gzip first, then chunked, and expect the final body.

- [ ] **Step 7: Add rejection tests**

Add tests for:

- `Transfer-Encoding: br, chunked` returns `STATUS_NOT_SUPPORTED`.
- `Transfer-Encoding: gzip, chunked` plus `Content-Length` returns `STATUS_INVALID_NETWORK_RESPONSE`.
- `Transfer-Encoding: chunked, chunked` returns `STATUS_INVALID_NETWORK_RESPONSE`.
- `Transfer-Encoding: gzip;foo=bar, chunked` returns `STATUS_INVALID_NETWORK_RESPONSE`.
- `Transfer-Encoding: ,` returns `STATUS_INVALID_NETWORK_RESPONSE`.
- `Transfer-Encoding: gzip,, chunked` succeeds by ignoring reasonable empty list members.
- `Transfer-Encoding: x-gzip, chunked` and `x-compress, chunked` decode through their RFC 9112 deprecated aliases.
- `HTTP/1.0 200 OK` with any `Transfer-Encoding` returns `STATUS_INVALID_NETWORK_RESPONSE`.
- A complete `Transfer-Encoding: chunked` header with no chunk bytes yet returns `STATUS_MORE_PROCESSING_REQUIRED`.

- [ ] **Step 8: Run test to verify current failures**

Run:

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'
```

Expected before implementation: at least `gzip, chunked`, `deflate, chunked`, `compress, chunked`, close-delimited gzip, and TE+CL tests fail.

## Chunk 2: Shared Coding Decoder

### Task 2: Extract gzip/deflate/brotli and add compress

**Files:**
- Create: `include/KernelHttp/http/HttpCoding.h`
- Create: `src/KernelHttpLib/http/HttpCoding.cpp`
- Modify: `src/KernelHttpLib/http/HttpContentEncoding.cpp`
- Modify: `src/KernelHttpLib/KernelHttpLib.vcxproj`
- Modify: `src/KernelHttpLib/KernelHttpLib.vcxproj.filters`
- Test: `tests/http_parser_tests.cpp`

- [ ] **Step 1: Create shared header**

Create `include/KernelHttp/http/HttpCoding.h`:

```cpp
#pragma once

#include <KernelHttp/http/HttpTypes.h>

namespace KernelHttp
{
namespace http
{
    enum class HttpCoding : UCHAR
    {
        Identity,
        Gzip,
        Deflate,
        Brotli,
        Compress
    };

    struct HttpCodingDecodeBuffers final
    {
        char* DecodedBody = nullptr;
        SIZE_T DecodedBodyCapacity = 0;
        char* ScratchBody = nullptr;
        SIZE_T ScratchBodyCapacity = 0;
    };

    struct HttpCodingDecodeResult final
    {
        const char* Body = nullptr;
        SIZE_T BodyLength = 0;
        bool AppliedCoding = false;
    };

    class HttpCodingCodec final
    {
    public:
        HttpCodingCodec() = delete;

        _Must_inspect_result_
        static NTSTATUS DecodeOne(
            HttpCoding coding,
            _In_reads_bytes_(sourceLength) const char* source,
            SIZE_T sourceLength,
            _Out_writes_bytes_(destinationCapacity) char* destination,
            SIZE_T destinationCapacity,
            _Out_ SIZE_T* decodedLength) noexcept;

        _Must_inspect_result_
        static NTSTATUS DecodeChainReverse(
            _In_reads_(codingCount) const HttpCoding* codings,
            SIZE_T codingCount,
            _In_reads_bytes_(bodyLength) const char* body,
            SIZE_T bodyLength,
            _In_ const HttpCodingDecodeBuffers& buffers,
            _Out_ HttpCodingDecodeResult& result) noexcept;
    };
}
}
```

- [ ] **Step 2: Move existing decoder internals**

Move these private helpers from `HttpContentEncoding.cpp` into `HttpCoding.cpp` without changing behavior:

- `DecodeRawDeflate`
- `DecodeDeflate`
- `DecodeGzip`
- `DecodeBrotli`
- CRC/adler helpers
- bounded allocation helpers
- `IsDecodedSizeAllowed`
- destination buffer selection

Keep the existing limits:

- `MaxDecodedBytes = 16 * 1024 * 1024`
- `MaxDecodeExpansionRatio = 64`

- [ ] **Step 3: Implement bounded `compress` decoder**

Add a UNIX compress LZW decoder for `HttpCoding::Compress`:

- Accept only `.Z` style streams with magic `0x1F 0x9D`.
- Reject block mode or max bits outside the implemented bounded range.
- Decode into caller-provided output only; no unbounded heap growth.
- Return `STATUS_BUFFER_TOO_SMALL` when the decoded output does not fit.
- Return `STATUS_INVALID_NETWORK_RESPONSE` for malformed code streams.
- Zero temporary tables before free.

Keep tables allocated from nonpaged pool through existing `new[]` or local allocation helpers.

- [ ] **Step 4: Refactor content decoding to call shared decoder**

Change `HttpContentEncoding::Decode` so it parses `Content-Encoding` tokens as before, maps them to `HttpCoding`, then calls `HttpCodingCodec::DecodeChainReverse`.

Keep content-coding behavior unchanged:

- `identity` is accepted.
- `gzip`, `deflate`, `br` are accepted.
- More than two content codings remains `STATUS_NOT_SUPPORTED`.
- Unknown content coding remains `STATUS_NOT_SUPPORTED`.

- [ ] **Step 5: Add project entries**

Add `http\HttpCoding.cpp` to `src/KernelHttpLib/KernelHttpLib.vcxproj` under `<ClCompile>`.

Add `..\..\include\KernelHttp\http\HttpCoding.h` to `<ClInclude>`.

Mirror both files in `src/KernelHttpLib/KernelHttpLib.vcxproj.filters`.

- [ ] **Step 6: Run content-encoding tests**

Run:

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'
```

Expected after this task: existing content-encoding tests still pass; new transfer-coding tests may still fail until `HttpTransferCoding` is wired.

## Chunk 3: Transfer-Coding Parser And Decoder

### Task 3: Implement transfer-coding chain semantics

**Files:**
- Create: `include/KernelHttp/http/HttpTransferCoding.h`
- Create: `src/KernelHttpLib/http/HttpTransferCoding.cpp`
- Modify: `include/KernelHttp/http/HttpResponse.h`
- Modify: `src/KernelHttpLib/http/HttpResponse.cpp`
- Modify: `src/KernelHttpLib/KernelHttpLib.vcxproj`
- Modify: `src/KernelHttpLib/KernelHttpLib.vcxproj.filters`
- Test: `tests/http_parser_tests.cpp`

- [ ] **Step 1: Create transfer-coding API**

Create `include/KernelHttp/http/HttpTransferCoding.h`:

```cpp
#pragma once

#include <KernelHttp/http/HttpCoding.h>
#include <KernelHttp/http/HttpResponse.h>

namespace KernelHttp
{
namespace http
{
    constexpr SIZE_T HttpMaxTransferCodings = 4;

    struct HttpTransferCodingInfo final
    {
        HttpCoding Codings[HttpMaxTransferCodings] = {};
        SIZE_T CodingCount = 0;
        bool HasTransferEncoding = false;
        bool FinalCodingIsChunked = false;
    };

    struct HttpTransferDecodeResult final
    {
        const char* Body = nullptr;
        SIZE_T BodyLength = 0;
        SIZE_T BytesConsumed = 0;
        HttpBodyKind BodyKind = HttpBodyKind::None;
    };

    class HttpTransferCoding final
    {
    public:
        HttpTransferCoding() = delete;

        _Must_inspect_result_
        static NTSTATUS Parse(
            _In_reads_(headerCount) const HttpHeader* headers,
            SIZE_T headerCount,
            _Out_ HttpTransferCodingInfo& info) noexcept;

        _Must_inspect_result_
        static NTSTATUS DecodeResponseBody(
            _In_ const HttpTransferCodingInfo& info,
            _In_reads_bytes_(wireBodyLength) const char* wireBody,
            SIZE_T wireBodyLength,
            bool messageCompleteOnConnectionClose,
            _In_ const HttpCodingDecodeBuffers& buffers,
            _Out_ HttpTransferDecodeResult& result) noexcept;
    };
}
}
```

- [ ] **Step 2: Parse all `Transfer-Encoding` field values in order**

In `HttpTransferCoding::Parse`:

- Iterate response headers in wire order.
- For each `Transfer-Encoding` header, parse comma-separated field values left to right.
- Trim OWS.
- Split optional transfer parameters at `;`; validate token is non-empty.
- Ignore empty comma-list elements only when at least one effective coding remains.
- Map:
  - `chunked` -> `HttpTransferCodingKind::Chunked`
  - `gzip` -> `HttpCoding::Gzip`
  - `deflate` -> `HttpCoding::Deflate`
  - `compress` -> `HttpCoding::Compress`
  - `x-gzip` -> `HttpCoding::Gzip`
  - `x-compress` -> `HttpCoding::Compress`
- Reject `identity` in `Transfer-Encoding` as `STATUS_INVALID_NETWORK_RESPONSE`.
- Reject `br` as `STATUS_NOT_SUPPORTED`.
- Reject unknown tokens as `STATUS_NOT_SUPPORTED`.
- Reject parameters on known transfer codings as `STATUS_INVALID_NETWORK_RESPONSE`.
- Reject more than `HttpMaxTransferCodings` as `STATUS_NOT_SUPPORTED`.
- Reject duplicate `chunked` as `STATUS_INVALID_NETWORK_RESPONSE`.

- [ ] **Step 3: Validate chain framing**

Rules:

- If no transfer coding is present, return `HasTransferEncoding = false`.
- If final transfer coding is `chunked`, the message is self-delimited by chunk decoding.
- If final transfer coding is not `chunked`, response parsing requires `messageCompleteOnConnectionClose = true`; otherwise return `STATUS_MORE_PROCESSING_REQUIRED`.
- Do not silently fall back to close-delimited parsing for unsupported transfer coding.

- [ ] **Step 4: Decode final chunked body**

For `gzip, chunked`, decode `chunked` first using existing `HttpParser::DecodeChunkedBody`, then decode earlier codings in reverse order with `HttpCodingCodec::DecodeChainReverse`.

Implementation detail: avoid recursive dependency by moving `DecodeChunkedBody` helper into `HttpTransferCoding.cpp`, or keep the public static `HttpParser::DecodeChunkedBody` call if that does not create include cycles.

- [ ] **Step 5: Decode final non-chunked close-delimited body**

For `Transfer-Encoding: gzip` and `Transfer-Encoding: chunked, gzip`, use the complete close-delimited body bytes only when `messageCompleteOnConnectionClose = true`, then decode codings in reverse order. For an inner `chunked` stream such as `chunked, gzip`, require the decoded chunked stream to consume all decoded bytes; trailing bytes after the terminating chunk are `STATUS_INVALID_NETWORK_RESPONSE`.

- [ ] **Step 6: Preserve bytes consumed**

For final `chunked`, set `BytesConsumed` to chunk decoder consumed bytes so pipelined bytes after the terminating chunk remain unread.

For final non-`chunked`, set `BytesConsumed = wireBodyLength` because close-delimited response consumes all available bytes.

- [ ] **Step 7: Add project entries**

Add `http\HttpTransferCoding.cpp` and `..\..\include\KernelHttp\http\HttpTransferCoding.h` to the project and filters.

- [ ] **Step 8: Run transfer-coding tests**

Run:

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'
```

Expected after this task: transfer-coding unit tests pass except parser integration tests that require `HttpParser` routing.

## Chunk 4: HttpParser Integration

### Task 4: Route HTTP/1.1 body framing through transfer-coding

**Files:**
- Modify: `src/KernelHttpLib/http/HttpParser.cpp`
- Modify: `include/KernelHttp/http/HttpParser.h`
- Test: `tests/http_parser_tests.cpp`

- [ ] **Step 1: Include transfer-coding header**

Add:

```cpp
#include <KernelHttp/http/HttpTransferCoding.h>
```

- [ ] **Step 2: Parse transfer-coding before content-length**

Inside `HttpParser::ParseResponse`, after headers and no-body checks:

```cpp
HttpTransferCodingInfo transferInfo = {};
status = HttpTransferCoding::Parse(response.Headers, response.HeaderCount, transferInfo);
if (!NT_SUCCESS(status)) {
    response = {};
    return status;
}

if (transferInfo.HasTransferEncoding) {
    bool hasContentLength = false;
    SIZE_T ignoredContentLength = 0;
    status = ReadContentLength(response, &hasContentLength, &ignoredContentLength);
    if (!NT_SUCCESS(status)) {
        response = {};
        return status;
    }
    if (hasContentLength) {
        response = {};
        return STATUS_INVALID_NETWORK_RESPONSE;
    }

    HttpCodingDecodeBuffers codingBuffers = {};
    codingBuffers.DecodedBody = options.DecodedBody;
    codingBuffers.DecodedBodyCapacity = options.DecodedBodyCapacity;
    codingBuffers.ScratchBody = options.ScratchBody;
    codingBuffers.ScratchBodyCapacity = options.ScratchBodyCapacity;

    HttpTransferDecodeResult transfer = {};
    status = HttpTransferCoding::DecodeResponseBody(
        transferInfo,
        data + headerEnd,
        dataLength - headerEnd,
        options.MessageCompleteOnConnectionClose,
        codingBuffers,
        transfer);
    if (!NT_SUCCESS(status)) {
        response = {};
        return status;
    }

    response.BodyKind = transfer.BodyKind;
    response.Body = transfer.Body;
    response.BodyLength = transfer.BodyLength;
    response.BytesConsumed = headerEnd + transfer.BytesConsumed;
    return ApplyContentEncoding(options, response, response.Body, response.BodyLength);
}
```

Do not keep the old `HasChunkedTransferEncoding()` branch for parser framing.

- [ ] **Step 3: Preserve content-coding order**

Transfer-coding must decode before `Content-Encoding`.

Example:

```text
Transfer-Encoding: gzip, chunked
Content-Encoding: br
```

Decode order:

1. chunked transfer
2. gzip transfer
3. br content

- [ ] **Step 4: Add 205 no-body status**

Update `StatusCodeHasNoBody` so `205 Reset Content` is body-forbidden.

- [ ] **Step 5: Run parser tests**

Run:

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'
```

Expected after this task: all HTTP parser tests pass.

## Chunk 5: Request-Side Rejection

### Task 5: Reject request `Transfer-Encoding` until upload coding is implemented

**Files:**
- Modify: `src/KernelHttpLib/http/HttpRequest.cpp`
- Modify: `src/KernelHttpLib/engine/HttpEngine.cpp`
- Modify: `tests/http_parser_tests.cpp`
- Modify: `tests/khttp_tests.cpp`

- [ ] **Step 1: Add request builder test**

In `tests/http_parser_tests.cpp`, add a request builder test:

```cpp
void TestRequestBuilderRejectsTransferEncoding()
{
    HttpHeader headers[1] = {
        { MakeText("Transfer-Encoding"), MakeText("chunked") }
    };

    HttpRequestBuildOptions options = {};
    options.Method = HttpMethod::Post;
    options.Path = MakeText("/upload");
    options.Host = MakeText("example.com");
    options.Body = "hello";
    options.BodyLength = 5;
    options.ExtraHeaders = headers;
    options.ExtraHeaderCount = 1;

    char request[256] = {};
    SIZE_T written = 0;
    const NTSTATUS status = HttpRequestBuilder::Build(options, request, sizeof(request), &written);

    Expect(status == STATUS_NOT_SUPPORTED, "request Transfer-Encoding is rejected until upload coding is implemented");
}
```

- [ ] **Step 2: Reject in `HttpRequestBuilder::Build`**

Before writing headers, scan `options.ExtraHeaders` and reject:

- `Transfer-Encoding`
- duplicate `Host`
- duplicate `Content-Length`
- duplicate `Connection`

`Host`, `Content-Length`, and `Connection` are already skipped in engine, but the lower-level builder should also defend itself.

- [ ] **Step 3: Reject in engine request header API**

In `BuildHttpRequestOptions`, change the header skip list:

```cpp
if (HeaderNameEquals(header, "Host") ||
    HeaderNameEquals(header, "Content-Length") ||
    HeaderNameEquals(header, "Connection")) {
    continue;
}
```

to reject `Transfer-Encoding` with `STATUS_NOT_SUPPORTED` instead of forwarding it.

- [ ] **Step 4: Add khttp/API test**

In `tests/khttp_tests.cpp`, create a request, set `Transfer-Encoding: chunked`, attach a body, send through the existing fake transport path, and assert `STATUS_NOT_SUPPORTED`.

Also add a fake transport response with `Transfer-Encoding: gzip, chunked` and assert the khttp response body is decoded to the original literal.

- [ ] **Step 5: Run request tests**

Run:

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\khttp_tests.exe'
```

Expected after this task: both pass.

## Chunk 6: Client And Engine Close-Delimited Behavior

### Task 6: Verify close-delimited transfer-coding works through clients

**Files:**
- Modify if needed: `src/KernelHttpLib/client/HttpClient.cpp`
- Modify if needed: `src/KernelHttpLib/client/HttpsClient.cpp`
- Modify if needed: `src/KernelHttpLib/engine/HttpEngine.cpp`
- Modify: `tests/high_level_api_tests.cpp`

- [ ] **Step 1: Add fake transport response for close-delimited gzip transfer-coding**

In `tests/high_level_api_tests.cpp`, add a fake server response:

```text
HTTP/1.1 200 OK\r\n
Transfer-Encoding: gzip\r\n
Connection: close\r\n
\r\n
<gzip bytes>
```

Make the fake receive path close after the gzip bytes.

- [ ] **Step 2: Assert high-level response body is decoded**

Expect `STATUS_SUCCESS`, status code `200`, and body equal to the uncompressed literal.

- [ ] **Step 3: Preserve existing connection pool safety**

Confirm connection reuse stays disabled for close-delimited transfer-coded responses because `Connection: close` or incomplete consumption prevents pooling.

- [ ] **Step 4: Run high-level tests**

Run:

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\high_level_api_tests.exe'
```

Expected after this task: high-level API tests pass.

## Chunk 7: Documentation

### Task 7: Update supported capability wording

**Files:**
- Modify: `README.md`
- Modify: `README_en.md`
- Modify: `docs/api-overview.md`
- Modify: `docs/high-level-api.md`

- [ ] **Step 1: Update HTTP/1.1 capability table**

Replace wording that says non-chunked transfer coding is unsupported with:

```text
Response Transfer-Encoding supports RFC 9112 chain parsing for chunked, gzip, deflate, and compress. Unknown transfer codings are rejected. Response trailers are consumed but not exposed.
```

- [ ] **Step 2: Document request-side boundary**

State:

```text
Request bodies currently use Content-Length. User-supplied Transfer-Encoding on requests is rejected; chunked upload is not supported yet.
```

- [ ] **Step 3: Document `br` distinction**

State:

```text
br is supported as Content-Encoding, not as Transfer-Encoding.
```

- [ ] **Step 4: Do not commit documentation**

Do not run `git add` or `git commit` unless the user explicitly asks.

## Chunk 8: Full Verification

### Task 8: Run protocol tests and Debug build

**Files:**
- No source edits.

- [ ] **Step 1: Run HTTP parser tests**

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'
```

Expected: `PASS: HTTP parser tests`

- [ ] **Step 2: Run high-level and khttp tests**

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\khttp_tests.exe'
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\high_level_api_tests.exe'
```

Expected: both pass. Public network examples that are already treated as environment failures must not be used as the sole proof.

- [ ] **Step 3: Run adjacent protocol tests**

```powershell
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http2_client_tests.exe'
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\tls_record_tests.exe'
pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\websocket_client_tests.exe'
```

Expected: all pass.

- [ ] **Step 4: Build Debug x64 with warnings as errors**

```powershell
pwsh -NoLogo -NoProfile -Command '& msbuild.exe .\KernelHttp.sln /p:Configuration=Debug /p:Platform=x64 /m'
```

Expected: build succeeds with `0 个警告` and `0 个错误`.

- [ ] **Step 5: Confirm project flags remain strict**

Confirm these remain present in `src/KernelHttpLib/KernelHttpLib.vcxproj` and `src/KernelHttpTest/KernelHttpTest.vcxproj`:

- `WarningLevel` = `EnableAllWarnings`
- `TreatWarningAsError` = `true`
- `ExceptionHandling` = `false`
- `RuntimeTypeInfo` = `false`

- [ ] **Step 6: Do not run forbidden smoke command**

Do not run:

```powershell
pwsh -NoLogo -NoProfile -File .\tests\integration\https_smoke.ps1 -Configuration Debug -Platform x64 -SkipDriverBuild
```

## Commit Note

Do not commit this plan or implementation unless the user explicitly asks. If the user asks for a commit after implementation, use a Conventional Commit message such as:

```text
feat(http): support response transfer-coding chains

- Add response Transfer-Encoding chain parsing and decoding
- Implement gzip, deflate, and compress transfer codings
- Reject invalid TE/CL response and request combinations
- Cover transfer-coding behavior with parser and high-level tests
```
