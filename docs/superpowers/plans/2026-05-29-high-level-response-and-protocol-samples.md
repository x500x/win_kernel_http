# High-Level Response And Protocol Samples Implementation Plan

> **For agentic workers:** REQUIRED: Execute in the current session. Sub-agents are not used unless the user explicitly requests delegation. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Print full high-level HTTP response headers and bodies, remove failing public WebSocket sample cases, and strengthen IPv4, IPv6, Any, HTTP/1.1, and HTTP/2 examples.

**Architecture:** Keep transport, TLS, and HTTP parser paths unchanged. Add read-only response header enumeration to the high-level API, then consume it from samples. Extend samples through existing request/session APIs so protocol coverage stays demonstrative rather than adding a parallel test framework.

**Tech Stack:** Windows kernel C++ under `/kernel`, khttp high-level API, existing `KERNEL_HTTP_USER_MODE_TEST` host tests, `pwsh`, MSBuild Debug driver build.

---

### Task 1: Response Header Enumeration API

**Files:**
- Modify: `src/KernelHttp/engine/Engine.h`
- Modify: `src/KernelHttp/engine/Engine.cpp`
- Modify: `src/KernelHttp/khttp/Response.h`
- Modify: `src/KernelHttp/khttp/Response.cpp`
- Test: `tests/khttp_tests.cpp`

- [x] Add `KhResponseHeaderCount` and `KhResponseGetHeaderAt` read-only engine APIs.
- [x] Add `khttp::ResponseHeaderCount` and `khttp::ResponseGetHeaderAt` wrappers.
- [x] Add a host test that parses a response with two headers and reads both by index.

### Task 2: Full Response Logging

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Test: `tests/high_level_api_tests.cpp`

- [x] Update `LogHttpResponse` to print every response header through the new API.
- [x] Print response body bytes for non-empty bodies with a bounded per-log preview to avoid oversize debug print calls.
- [x] Update callback body logging to include body chunk content.
- [x] Keep HEAD/OPTIONS zero-body behavior explicit.

### Task 3: Protocol And Address-Family Samples

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.h`
- Test: `tests/high_level_api_tests.cpp`

- [x] Add HTTP/1.1 samples for IPv4, IPv6, and Any address families.
- [x] Add HTTPS HTTP/1.1 and HTTPS HTTP/2 samples with explicit ALPN configuration.
- [x] Ensure logs include protocol intent and address family.
- [x] Keep existing verb/body/async examples unchanged except for richer output.

### Task 4: WebSocket Sample Fix

**Files:**
- Modify: `src/KernelHttp/samples/HighLevelApiSamples.cpp`
- Test: `tests/high_level_api_tests.cpp`

- [x] Change URL-overload WebSocket samples from failing public `ws://` endpoint to the known working `wss://` endpoint.
- [x] Preserve coverage for URL, config, Ex, sync, async, text, text Ex, binary, binary Ex, and receive callback APIs.
- [x] Avoid introducing fallback/retry architecture.

### Task 5: Verification

**Files:**
- Existing test scripts only.

- [x] Run high-level/khttp host regression tests, not smoke-only tests.
- [x] Run the broader integration regression script when available.
- [x] Run Debug driver build after tests without adding artificial timeout waits.
- [x] Review `git diff` for unrelated churn before final response.
