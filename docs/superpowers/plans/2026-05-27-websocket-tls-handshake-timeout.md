# WebSocket TLS Handshake Timeout Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking. Do not commit this plan or implementation unless the user explicitly asks for a git commit.

**Goal:** Avoid false `STATUS_IO_TIMEOUT` failures while reading TLS handshake records for high-level WebSocket certificate-verified connections.

**Architecture:** Keep WSK receive default behavior unchanged, but allow callers to pass an explicit receive timeout. TLS handshake reads use a configurable handshake receive timeout, and high-level WebSocket options carry that value into the TLS connection.

**Tech Stack:** Windows kernel C++ (`/kernel`), WSK, custom TLS, user-mode C++ tests, MSBuild.

---

### Task 1: WSK Receive Timeout Parameter

**Files:**
- Modify: `src/KernelHttp/net/WskSocket.h`
- Modify: `src/KernelHttp/net/WskSocket.cpp`
- Modify: user-mode test stubs with matching signatures

- [x] Add optional `timeoutMilliseconds` to both `WskSocket::Receive` overloads.
- [x] Keep `WskOperationTimeoutMilliseconds` as the default.
- [x] Pass the timeout into `CompleteSyncIrp`.

### Task 2: TLS Handshake Timeout

**Files:**
- Modify: `src/KernelHttp/KernelHttpConfig.h`
- Modify: `src/KernelHttp/tls/TlsConnection.h`
- Modify: `src/KernelHttp/tls/TlsConnection.cpp`

- [x] Add `TlsHandshakeReceiveTimeoutMilliseconds`.
- [x] Add `HandshakeReceiveTimeoutMilliseconds` to `TlsClientConnectionOptions`.
- [x] Use that timeout only for handshake `ReadExact` calls.

### Task 3: High-Level WebSocket Option Chain

**Files:**
- Modify: `src/KernelHttp/api/KernelHttpApi.h`
- Modify: `src/KernelHttp/api/KernelHttpApi.cpp`
- Modify: `src/KernelHttp/client/WebSocketClient.h`
- Modify: `src/KernelHttp/client/WebSocketClient.cpp`

- [x] Add defaulted handshake timeout to `KhTlsOptions`.
- [x] Validate that the value is nonzero.
- [x] Copy the field through sync and async WebSocket connect paths.
- [x] Pass the value to `TlsClientConnectionOptions`.

### Task 4: Tests And Build

**Files:**
- Modify: `tests/high_level_api_tests.cpp`
- Modify: `tests/websocket_client_tests.cpp` or stubs as needed

- [x] Assert high-level defaults expose the TLS handshake timeout.
- [x] Assert WebSocket sync and async paths preserve the configured timeout in user-mode test transport.
- [x] Run the focused tests.
- [x] Run the test suite and Debug build.
