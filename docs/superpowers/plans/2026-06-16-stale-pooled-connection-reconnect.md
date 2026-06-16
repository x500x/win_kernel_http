# Stale Pooled Connection Reconnect Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the high-level HTTP connection pool recover when a reused pooled connection has been silently closed.

**Architecture:** Keep connection ownership in `KhConnectionPool`, but make `HttpEngine` classify reused transport failures as stale-connection failures. Failed pooled entries are closed before any retry, and only replay-safe methods (`GET`, `HEAD`, `OPTIONS`) retry on a fresh connection.

**Tech Stack:** C++ kernel-style code, WSK/TLS transports, user-mode test transport, `khttp_tests`.

---

## File Structure

- Modify: `src/KernelHttpLib/engine/HttpEngine.cpp`
  - Add a small helper that decides when a failed request invalidates a pooled entry.
  - Ensure retry cleanup closes the failed entry immediately and does not leave stale resources in the pool.
- Modify: `tests/khttp_tests.cpp`
  - Add focused tests for silent close cleanup and fresh reconnect behavior.
  - Reuse the existing user-mode transport callback model.

## Chunk 1: Tests And Engine Cleanup

### Task 1: Add stale pooled connection cleanup tests

**Files:**
- Modify: `tests/khttp_tests.cpp`

- [x] **Step 1: Add a capture that fails only reused connections**

Track call count, reused call count, new connection count, and connection ids.

- [x] **Step 2: Add GET recovery test**

Run one successful GET to seed the pool, make the reused call return `STATUS_CONNECTION_DISCONNECTED`, then verify the same request succeeds through a fresh connection id.

- [x] **Step 3: Add POST non-replay test**

Seed the pool with GET, issue POST, verify the reused failure is returned and no fresh retry occurs.

### Task 2: Tighten engine stale cleanup

**Files:**
- Modify: `src/KernelHttpLib/engine/HttpEngine.cpp`

- [x] **Step 1: Add helper for stale pooled failure**

Classify connection-close statuses, `STATUS_RETRY`, and timeout as retry candidates only through the existing safe-method gate.

- [x] **Step 2: Close failed pooled entry before retry**

When stale failure is detected, call `KhConnectionPoolClose` on the failed entry and null the pointer before force-acquiring a fresh entry.

- [x] **Step 3: Preserve non-safe behavior**

For POST/PUT/PATCH/DELETE, release with `reusable=false` so the bad connection is removed but the request is not replayed.

### Task 3: Verification

**Files:**
- Test: `tests/khttp_tests.cpp`
- Build: `KernelHttp.sln`

- [x] **Step 1: Run focused test binary**

Run the project test target that includes `khttp_tests`.

- [x] **Step 2: Run Debug build**

Run Debug x64 build with warnings as errors. Do not run the prohibited HTTPS smoke script.
