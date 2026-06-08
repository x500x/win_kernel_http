# Protocol Completeness Remediation Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the supported HTTP/WebSocket/TLS stack closer to protocol-correct behavior, while documenting unsupported optional features honestly and preserving Windows kernel constraints.

**Architecture:** Fix correctness gaps in small layers: tests first, then WebSocket message aggregation, TLS version negotiation classification, HTTP/2 stream termination/error handling, certificate SAN policy, and documentation. TLS1.2-only handling is a verified negotiation path, not an unconditional retry after arbitrary TLS failure.

**Tech Stack:** Windows kernel C++ under `/kernel`, WSK, kernel CNG/BCrypt, existing KernelHttp protocol modules, user-mode protocol tests, MSBuild Debug x64. All commands use `pwsh`; do not run the forbidden `tests/integration/https_smoke.ps1 -SkipDriverBuild` command.

---

## Chunk 1: Capability Contract

### Task 1: Update protocol capability wording

**Files:**
- Modify: `README.md`
- Modify: `README_en.md`
- Modify: `docs/api-overview.md`
- Modify: `docs/high-level-api.md`
- Reference: `docs/plans/2026-06-08-protocol-completeness-design.md`

- [ ] Replace broad claims such as full HTTP/2, full WebSocket, or full TLS with precise supported-subset wording.
- [ ] Add a table for unsupported optional features: WebSocket extensions, WebSocket receive fragment callback, HTTP/2 push, TLS client certificates, TLS CBC/ChaCha20, OCSP/CRL revocation, IDNA.
- [ ] State that synchronous HTTP/WebSocket/TLS APIs require `PASSIVE_LEVEL`.
- [ ] State that TLS1.2 selection after a TLS1.3 attempt is allowed only after verified version negotiation evidence.
- [ ] Do not commit the documentation unless the user explicitly asks.

## Chunk 2: WebSocket Receive Correctness

### Task 2: Add failing receive-fragment tests

**Files:**
- Modify: `tests/websocket_client_tests.cpp`
- Modify if needed: `tests/websocket_frame_tests.cpp`

- [ ] Add a fake server script that returns a text message as `FIN=0 Text`, an interleaved Ping, and `FIN=1 Continuation`.
- [ ] Assert that `WebSocketClient::ReceiveMessage` returns one complete Text message with the concatenated payload and auto-sends Pong.
- [ ] Add binary fragmentation coverage with at least two continuation frames.
- [ ] Add protocol-error tests for continuation without an open message and new Text/Binary frame while a fragmented message is open.
- [ ] Add a subprotocol mismatch test: client requests `chat`, server responds `other`, expected `STATUS_INVALID_NETWORK_RESPONSE`.
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\websocket_client_tests.exe'`.
- [ ] Expected before implementation: new tests fail.

### Task 3: Implement WebSocket message aggregation

**Files:**
- Modify: `include/KernelHttp/client/WebSocketClient.h`
- Modify: `src/KernelHttpLib/client/WebSocketClient.cpp`
- Modify: `src/KernelHttpLib/engine/WsEngine.cpp`
- Test: `tests/websocket_client_tests.cpp`

- [ ] Add receive-side state to `WebSocketClient`: fragment-open flag, message opcode, accumulated length, and optional UTF-8 state for Text.
- [ ] Change `ReceiveMessage` to loop until it has one complete message or a Close frame.
- [ ] Keep control frame handling in the receive loop: Ping sends Pong immediately; Pong does not complete the user message.
- [ ] Enforce RFC 6455 fragmentation rules: first data frame is Text/Binary, continuations only after an open message, no new data frame until final continuation.
- [ ] Enforce `outputCapacity`; if the assembled message is too large, return `STATUS_BUFFER_TOO_SMALL` and leave the connection state consistent.
- [ ] Preserve server-frame validation: server frames must be unmasked, RSV must be zero, control frames must be final and <= 125 bytes.
- [ ] Update `KhWebSocketReceiveSyncImpl` so default receive returns complete messages and does not always hard-code fragment metadata unrelated to the real message path.
- [ ] Re-run `websocket_frame_tests.exe` and `websocket_client_tests.exe`; expected PASS.

### Task 4: Validate WebSocket subprotocol response

**Files:**
- Modify: `include/KernelHttp/websocket/WebSocketFrame.h`
- Modify: `src/KernelHttpLib/websocket/WebSocketFrame.cpp`
- Modify: `src/KernelHttpLib/client/WebSocketClient.cpp`
- Test: `tests/websocket_client_tests.cpp`

- [ ] Extend handshake validation to accept the requested subprotocol list or a single requested subprotocol.
- [ ] If the client requested no subprotocol, reject any server `Sec-WebSocket-Protocol`.
- [ ] If the client requested one or more subprotocols, accept only an exact token match from that list.
- [ ] Reject duplicate or empty selected subprotocol values.
- [ ] Re-run `websocket_client_tests.exe`; expected PASS.

## Chunk 3: TLS Version Negotiation

### Task 5: Add TLS version classification tests

**Files:**
- Modify: `tests/tls_record_tests.cpp`
- Create if needed: `include/KernelHttp/tls/TlsVersionNegotiation.h`
- Create if needed: `src/KernelHttpLib/tls/TlsVersionNegotiation.cpp`
- Modify if new source is created: `src/KernelHttpLib/KernelHttpLib.vcxproj`

- [ ] Add unit tests for parsing/classifying ServerHello when the client offered TLS1.2-TLS1.3.
- [ ] Cover TLS1.3 selected through `supported_versions`.
- [ ] Cover TLS1.2 ServerHello without TLS1.3 downgrade sentinel, classified as a TLS1.2 negotiation candidate.
- [ ] Cover TLS1.2 ServerHello with TLS1.3 downgrade sentinel, expected `STATUS_INVALID_NETWORK_RESPONSE`.
- [ ] Cover certificate error, ALPN mismatch, TCP timeout, Finished failure, and TLS1.3 CertificateRequest as non-candidates for TLS1.2-only classification.
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\tls_record_tests.exe'`.
- [ ] Expected before implementation: new classification tests fail.

### Task 6: Implement verified TLS1.2 selection

**Files:**
- Modify: `include/KernelHttp/tls/TlsConnection.h`
- Modify: `src/KernelHttpLib/tls/TlsConnection.cpp`
- Modify if helper is added: `include/KernelHttp/tls/TlsVersionNegotiation.h`
- Modify if helper is added: `src/KernelHttpLib/tls/TlsVersionNegotiation.cpp`
- Test: `tests/tls_record_tests.cpp`
- Test if transport fixtures exist: `tests/high_level_api_tests.cpp`

- [ ] Add an internal enum for version negotiation result: `Tls13Selected`, `Tls12Selected`, `RejectedDowngrade`, `NotVersionRelated`.
- [ ] When min/max allow both TLS1.2 and TLS1.3, classify the early ServerHello/alert before deciding whether TLS1.2 is valid.
- [ ] Continue TLS1.3 when the server selects TLS1.3.
- [ ] Continue or reconnect with TLS1.2 only when evidence proves a TLS1.2 negotiation candidate and the subsequent TLS1.2 handshake completes with valid certificate, ALPN, and Finished.
- [ ] Reject TLS1.2 ServerHello containing TLS1.3 downgrade sentinel.
- [ ] Never classify certificate validation failure, ALPN mismatch, TCP/WSK timeout, memory failure, user cancellation, record authentication failure, or Finished verification failure as TLS1.2-only.
- [ ] Keep the original error when TLS1.2-only confirmation fails for a non-version reason.
- [ ] Add diagnostic logging that reports the classification without leaking secrets.
- [ ] Re-run `tls_record_tests.exe`, `khttp_tests.exe`, and `high_level_api_tests.exe`; expected PASS.

### Task 7: Add optional origin capability cache

**Files:**
- Modify: `include/KernelHttp/engine/HandleTypes.h`
- Modify: `include/KernelHttp/engine/Engine.h`
- Modify: `src/KernelHttpLib/engine/Engine.cpp`
- Modify: `src/KernelHttpLib/engine/HttpEngine.cpp`
- Test: `tests/khttp_tests.cpp`

- [ ] Add cache entries for `Tls13Capable`, `ConfirmedTls12Only`, `Unknown`, and `RejectedDowngrade`.
- [ ] Key the cache by host, port, SNI, ALPN list, min/max TLS version, certificate policy, and trust store identity.
- [ ] Use `ConfirmedTls12Only` only to shorten future version selection; do not change certificate, ALPN, or pin policy.
- [ ] Add tests that a certificate policy change does not reuse a cached TLS1.2-only decision.
- [ ] This task may be deferred if Task 6 keeps verification correct without cache.

## Chunk 4: HTTP/2 Correctness

### Task 8: Add HTTP/2 termination and error tests

**Files:**
- Modify: `tests/http2_client_tests.cpp`
- Modify: `tests/http2_frame_tests.cpp`

- [ ] Add a scripted response with HEADERS and partial DATA but no `END_STREAM`; simulate timeout and expect `STATUS_IO_TIMEOUT`.
- [ ] Add illegal CONTINUATION tests: continuation without HEADERS, wrong stream id, and non-CONTINUATION while continuation is expected.
- [ ] Add PUSH_PROMISE test when `ENABLE_PUSH=0`; expect connection-level protocol error handling.
- [ ] Add RST_STREAM test that maps to failure and does not return partial success.
- [ ] Run: `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http2_client_tests.exe'`.
- [ ] Expected before implementation: at least the partial-response timeout test fails.

### Task 9: Fix HTTP/2 stream completion and error frames

**Files:**
- Modify: `include/KernelHttp/http2/Http2Connection.h`
- Modify: `src/KernelHttpLib/http2/Http2Connection.cpp`
- Modify if needed: `src/KernelHttpLib/http2/Http2Frame.cpp`
- Test: `tests/http2_client_tests.cpp`

- [ ] Remove the path that breaks out successfully on timeout after partial body before `END_STREAM`.
- [ ] Track stream terminal state explicitly: response headers received, body bytes received, END_STREAM received, RST_STREAM received, GOAWAY received.
- [ ] Return `STATUS_IO_TIMEOUT` when the stream has not reached a valid terminal state.
- [ ] Add helpers to send `RST_STREAM(PROTOCOL_ERROR)` and `GOAWAY(PROTOCOL_ERROR)`.
- [ ] Send `RST_STREAM` for stream-level protocol errors on the target stream.
- [ ] Send `GOAWAY` for connection-level protocol errors, including illegal stream 0 frames and forbidden PUSH_PROMISE.
- [ ] Keep unknown extension frames ignored as required by HTTP/2.
- [ ] Re-run `http2_frame_tests.exe`, `hpack_tests.exe`, and `http2_client_tests.exe`; expected PASS.

## Chunk 5: HTTP/1.1 Boundaries

### Task 10: Lock HTTP/1.1 behavior with tests and documentation

**Files:**
- Modify: `tests/http_parser_tests.cpp`
- Modify: `docs/high-level-api.md`
- Modify if behavior changes: `src/KernelHttpLib/http/HttpParser.cpp`
- Modify if behavior changes: `src/KernelHttpLib/engine/HttpEngine.cpp`

- [ ] Add tests that non-chunked `Transfer-Encoding` remains `STATUS_NOT_SUPPORTED` and is never treated as close-delimited when the transfer coding is unsupported.
- [ ] Add tests that chunk trailers are consumed without leaking bytes into the next response.
- [ ] Add tests that 1xx intermediate responses are skipped by high-level engine and not exposed as final responses, except 101 upgrade.
- [ ] Document that request chunked upload and response trailer exposure are currently unsupported.
- [ ] Re-run `http_parser_tests.exe` and `khttp_tests.exe`; expected PASS.

## Chunk 6: Certificate Validation

### Task 11: Add IP SAN and revocation-policy tests

**Files:**
- Modify: `include/KernelHttp/tls/CertificateValidator.h`
- Modify: `src/KernelHttpLib/tls/CertificateValidator.cpp`
- Modify: `tests/tls_record_tests.cpp`

- [ ] Add DER fixtures or generated test certificates with iPAddress SAN for IPv4 and IPv6.
- [ ] Add tests that IP literal hosts match iPAddress SAN and do not match dNSName wildcard rules.
- [ ] Add tests that dNSName SAN still takes priority over CommonName.
- [ ] Add a `RequireRevocationCheck` option test that returns `STATUS_NOT_SUPPORTED` until OCSP/CRL is implemented.
- [ ] Run `tls_record_tests.exe`; expected fail before implementation for IP SAN/revocation policy.

### Task 12: Implement IP SAN and explicit revocation policy

**Files:**
- Modify: `include/KernelHttp/tls/CertificateValidator.h`
- Modify: `src/KernelHttpLib/tls/CertificateValidator.cpp`
- Modify if exposed to API: `include/KernelHttp/engine/Engine.h`
- Modify if exposed to khttp: `include/KernelHttp/khttp/Types.h`
- Test: `tests/tls_record_tests.cpp`

- [ ] Extend `ParsedCertificate` to store a small bounded list of iPAddress SAN entries.
- [ ] Parse GeneralName tag `0x87` for IPv4 4-byte and IPv6 16-byte values.
- [ ] Detect whether `options.HostName` is an IPv4 or IPv6 literal.
- [ ] For IP literal hosts, require iPAddress SAN match; do not fall back to CN.
- [ ] For DNS hosts, keep existing dNSName then CN behavior.
- [ ] Add `RequireRevocationCheck` only if it can be wired through the public options without changing default behavior.
- [ ] If revocation is required but no OCSP/CRL implementation exists, return `STATUS_NOT_SUPPORTED`.
- [ ] Re-run `tls_record_tests.exe`; expected PASS.

## Chunk 7: Full Verification

### Task 13: Run user-mode protocol test suite

**Files:**
- No source edits.

- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http_parser_tests.exe'`.
- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\websocket_frame_tests.exe'`.
- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\websocket_client_tests.exe'`.
- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http2_frame_tests.exe'`.
- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\hpack_tests.exe'`.
- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\http2_client_tests.exe'`.
- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\tls_record_tests.exe'`.
- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\khttp_tests.exe'`.
- [ ] Run `pwsh -NoLogo -NoProfile -Command '& .\tests\out\bin\high_level_api_tests.exe'`.
- [ ] Do not run `pwsh -NoLogo -NoProfile -File .\tests\integration\https_smoke.ps1 -Configuration Debug -Platform x64 -SkipDriverBuild`.

### Task 14: Build Debug x64 with warnings as errors

**Files:**
- No source edits.

- [ ] Run `pwsh -NoLogo -NoProfile -Command '& msbuild.exe .\KernelHttp.sln /p:Configuration=Debug /p:Platform=x64 /m'`.
- [ ] Confirm the project still has `WarningLevel=EnableAllWarnings`, `TreatWarningAsError=true`, exceptions disabled, and RTTI disabled.
- [ ] If Release is built later, use the same warning-as-error expectations.
- [ ] Do not commit docs or code unless the user explicitly asks.
