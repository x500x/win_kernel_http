# High Level HTTP WebSocket Samples Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand the high-level khttp samples so Session, HTTP, async HTTP, response helpers, and WebSocket APIs all have Chinese-commented examples with Chinese request/response logging.

**Architecture:** Keep the existing `RunHighLevelApiSamples` entry point and extend its sample matrix instead of adding a second runner. Add focused helpers for logging, HTTP send variants, async handling, request body examples, and WebSocket examples while preserving the test transport path.

**Tech Stack:** Windows kernel C++ under `/kernel`, khttp high-level API, existing test transport in `KERNEL_HTTP_USER_MODE_TEST`, Visual Studio/MSBuild project.

---

## Chunk 1: Sample Surface

### Task 1: Expand result structs

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.h`

- [ ] Add fields for Session configuration examples.
- [ ] Add fields for HTTP shortcut APIs, Send/SendEx variants, async variants, request body variants, response header lookup, and WebSocket variants.
- [ ] Keep existing field names where possible so older callers still compile.

### Task 2: Add Chinese logging helpers

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`

- [ ] Add helpers that print Session config, HTTP request info, HTTP response info, async state, and WebSocket request/response info in Chinese.
- [ ] Print method, URL, body kind/length, TLS policy, address family, connection policy, status, status code, selected header, and body length for HTTP.
- [ ] Print URL, subprotocol, TLS policy, address family, send type/length, receive type/length, and close status for WebSocket.

## Chunk 2: HTTP Coverage

### Task 3: Cover Session APIs

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Modify: `tests/high_level_api_tests.cpp`

- [ ] Demonstrate default `SessionConfig` values with Chinese output.
- [ ] Demonstrate custom `SessionConfig` by creating and closing a temporary session.
- [ ] Update tests to verify one extra session can be created without affecting the main sample session.

### Task 4: Cover synchronous HTTP APIs

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Modify: `tests/high_level_api_tests.cpp`

- [ ] Add samples for `Get`, `Post`, `Put`, `Patch`, `Delete`, `Head`, and `Options`.
- [ ] Add samples for `Send`, `Send` with options, and `SendEx`.
- [ ] Add `ResponseGetHeader` usage and output the selected response header.
- [ ] Update tests to count all expected HTTP/HTTPS calls through the test transport.

### Task 5: Cover request body helpers

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Modify: `tests/high_level_api_tests.cpp`

- [ ] Add samples for `RequestSetTextBody`, `RequestSetJsonBody`, `RequestSetRawBody`, `RequestSetFormBody`, `RequestSetMultipartBody`, `RequestSetFileBody`, and `RequestClearBody`.
- [ ] Use existing `tests/testdata/request_body_file.txt` in user-mode tests and a documented sample path for kernel-mode runs.

### Task 6: Cover async HTTP APIs

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Modify: `tests/high_level_api_tests.cpp`

- [ ] Add samples for `GetAsync`, `PostAsync`, `SendAsync`, `SendAsync` with options, and `SendAsyncEx`.
- [ ] Demonstrate `AsyncWait`, `AsyncGetStatus`, `AsyncIsCompleted`, `AsyncGetResponse`, and `AsyncRelease`.
- [ ] Demonstrate `AsyncCancel` and `AsyncIsCanceled` on a standalone operation without adding a network dependency.

## Chunk 3: WebSocket Coverage

### Task 7: Cover WebSocket connect/send/receive APIs

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Modify: `tests/high_level_api_tests.cpp`

- [ ] Add samples for URL connect, config connect, and `WsConnectEx`.
- [ ] Add samples for `WsSendText`, `WsSendTextEx`, `WsSendBinary`, and `WsSendBinaryEx`.
- [ ] Add samples for `WsReceive` and `WsReceiveEx` with callback.
- [ ] Keep `WsClose` in every successful connect path.

### Task 8: Cover async WebSocket APIs

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Modify: `tests/high_level_api_tests.cpp`

- [ ] Add samples for `WsConnectAsync`, config `WsConnectAsync`, and `WsConnectAsyncEx`.
- [ ] Demonstrate `AsyncGetWebSocket` and close the returned socket.
- [ ] Update WebSocket test transport counters for text, binary, final-fragment options, callback receive, and async connections.

## Chunk 4: Verification

### Task 9: Update tests

**Files:**
- Modify: `tests/high_level_api_tests.cpp`

- [ ] Keep existing IPv4, TLS store, and TLS 1.2 WebSocket expectations.
- [ ] Add expectations for new HTTP call counts, WebSocket call counts, binary/text sends, close counts, and callback receive.
- [ ] Keep tests deterministic with the existing fake transport.

### Task 10: Build and test

**Commands:**
- Run: `pwsh -NoLogo -NoProfile -Command '.\tests\high_level_api_tests.exe'`
- Run: `pwsh -NoLogo -NoProfile -Command '.\gradlew.bat test'` if Gradle files exist.
- Run: Debug build with the repo's available Visual Studio/MSBuild command.

- [ ] Run the focused high-level API test.
- [ ] Run the repository test target, but do not run smoke tests.
- [ ] Run Debug build.
- [ ] If Gradle exists, build Debug without adding timeout waiting.
