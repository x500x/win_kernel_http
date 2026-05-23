# TLS 1.3 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking. Do not create git commits unless the user explicitly asks for commits.

**Goal:** Add complete TLS 1.3 client support with tests and samples.

**Architecture:** Keep TLS 1.2 stable while adding TLS 1.3-specific handshake, key schedule, record protection, session ticket, and early data paths. `TlsConnection` selects TLS 1.3 first, falls back to the existing TLS 1.2 path only when the server negotiates TLS 1.2, and exposes negotiated behavior to HTTPS and HTTP/2 clients.

**Tech Stack:** Windows kernel C++ `/kernel` subset, WSK, kernel CNG/BCrypt, AES-GCM, HKDF/HMAC-SHA256/SHA384, ECDHE, existing certificate validator, existing user-mode C++ tests, `pwsh` integration script.

---

## File Structure

- Modify: `src/KernelHttp/crypto/CngProvider.h`
  - Add HKDF extract/expand APIs.

- Modify: `src/KernelHttp/crypto/CngProvider.cpp`
  - Implement HKDF using existing HMAC in kernel and user-mode test paths.

- Modify: `src/KernelHttp/tls/TlsContext.h`
  - Add TLS 1.3 protocol, cipher suites, secret constants, ticket structures, and TLS 1.3 context APIs.

- Modify: `src/KernelHttp/tls/TlsContext.cpp`
  - Implement TLS 1.3 secret storage, cipher metadata, HKDF key schedule, and traffic key configuration.

- Modify: `src/KernelHttp/tls/TlsRecord.h`
  - Add TLS 1.3 AEAD constants and record protection declarations.

- Modify: `src/KernelHttp/tls/TlsRecord.cpp`
  - Implement TLS 1.3 nonce/AAD/content-type handling.

- Create: `src/KernelHttp/tls/TlsHandshake13.h`
  - Declare TLS 1.3 handshake structures, extension parsing, ClientHello encoding, Finished and CertificateVerify helpers.

- Create: `src/KernelHttp/tls/TlsHandshake13.cpp`
  - Implement TLS 1.3 handshake encoding/parsing and transcript helpers.

- Modify: `src/KernelHttp/tls/TlsConnection.h`
  - Add TLS version/session/early-data options and TLS 1.3 private helpers.

- Modify: `src/KernelHttp/tls/TlsConnection.cpp`
  - Add TLS 1.3 connection flow and preserve TLS 1.2 flow.

- Modify: `src/KernelHttp/client/HttpsClient.h`
  - Add TLS version/session/early-data request options.

- Modify: `src/KernelHttp/client/HttpsClient.cpp`
  - Pass TLS 1.3 options to `TlsConnection`.

- Modify: `src/KernelHttp/samples/HttpVerbSamples.h`
  - Add TLS 1.3 sample result fields.

- Modify: `src/KernelHttp/samples/HttpVerbSamples.cpp`
  - Add TLS 1.3 HTTPS, HTTP/2, and no-verify samples.

- Modify: `src/KernelHttp/KernelHttp.vcxproj`
  - Add `TlsHandshake13.cpp` and `.h`.

- Modify: `src/KernelHttp/KernelHttp.vcxproj.filters`
  - Add TLS 1.3 files to the TLS filter.

- Modify: `tests/tls_record_tests.cpp`
  - Add TLS 1.3 unit coverage.

- Modify: `tests/integration/https_smoke.ps1`
  - Compile TLS 1.3 source in host tests and add TLS 1.3-only local server mode.

---

## Chunk 1: Crypto and TLS 1.3 record foundation

### Task 1: Add HKDF APIs

**Files:**
- Modify: `src/KernelHttp/crypto/CngProvider.h`
- Modify: `src/KernelHttp/crypto/CngProvider.cpp`
- Test: `tests/tls_record_tests.cpp`

- [ ] **Step 1: Add failing HKDF tests**

Add tests that call `CngProvider::HkdfExtract` and `CngProvider::HkdfExpand` with deterministic inputs and assert output length and stable nonzero bytes in user-mode tests.

- [ ] **Step 2: Declare HKDF APIs**

Add `HkdfExtract` and `HkdfExpand` to `CngProvider`.

- [ ] **Step 3: Implement HKDF**

Use HMAC for extract and RFC 5869 expand loop with info and counter bytes. Reject zero output, oversized output, invalid buffers, and digest-length overflow.

- [ ] **Step 4: Run host TLS tests**

Run: `pwsh -NoLogo -NoProfile -File tests\integration\https_smoke.ps1 -SkipDriverBuild`

Expected: TLS tests compile and the new HKDF tests pass.

### Task 2: Add TLS 1.3 record protection

**Files:**
- Modify: `src/KernelHttp/tls/TlsRecord.h`
- Modify: `src/KernelHttp/tls/TlsRecord.cpp`
- Test: `tests/tls_record_tests.cpp`

- [ ] **Step 1: Add failing record tests**

Cover TLS 1.3 protect/unprotect, sequence increment, content type recovery, padding stripping, and buffer-too-small behavior.

- [ ] **Step 2: Add TLS 1.3 record API declarations**

Declare `ProtectAesGcm13` and `UnprotectAesGcm13`.

- [ ] **Step 3: Implement TLS 1.3 record protection**

Use legacy outer version `0x0303`, outer content type `application_data`, AAD as the 5-byte header, nonce as static IV XOR sequence number, and inner plaintext as `fragment || inner_type || zero_padding`.

- [ ] **Step 4: Run host TLS tests**

Run: `pwsh -NoLogo -NoProfile -File tests\integration\https_smoke.ps1 -SkipDriverBuild`

Expected: existing TLS 1.2 record tests and TLS 1.3 record tests pass.

---

## Chunk 2: TLS 1.3 handshake and key schedule

### Task 3: Add TLS 1.3 context state

**Files:**
- Modify: `src/KernelHttp/tls/TlsContext.h`
- Modify: `src/KernelHttp/tls/TlsContext.cpp`
- Test: `tests/tls_record_tests.cpp`

- [ ] **Step 1: Add failing context tests**

Cover TLS 1.3 initialization, cipher suite metadata, handshake traffic secret derivation, application traffic key configuration, and rejection of TLS 1.3 cipher suites in TLS 1.2 key block APIs.

- [ ] **Step 2: Add TLS 1.3 cipher suites and state**

Add `TlsAes128GcmSha256`, `TlsAes256GcmSha384`, traffic secret buffers, digest length helpers, and 12-byte IV state.

- [ ] **Step 3: Implement TLS 1.3 key schedule helpers**

Implement early secret, handshake secret, master secret, finished key, traffic key, IV, and resumption secret derivation via HKDF labels.

- [ ] **Step 4: Run host TLS tests**

Expected: context tests pass without breaking TLS 1.2 tests.

### Task 4: Add TLS 1.3 handshake parser/encoder

**Files:**
- Create: `src/KernelHttp/tls/TlsHandshake13.h`
- Create: `src/KernelHttp/tls/TlsHandshake13.cpp`
- Modify: `src/KernelHttp/KernelHttp.vcxproj`
- Modify: `src/KernelHttp/KernelHttp.vcxproj.filters`
- Modify: `tests/integration/https_smoke.ps1`
- Test: `tests/tls_record_tests.cpp`

- [ ] **Step 1: Add failing ClientHello tests**

Assert TLS 1.3 ClientHello contains `supported_versions`, `key_share`, `psk_key_exchange_modes`, SNI, ALPN, and optional PSK extensions.

- [ ] **Step 2: Add failing parser tests**

Cover ServerHello, HelloRetryRequest, EncryptedExtensions ALPN, Certificate, CertificateVerify, Finished, and NewSessionTicket.

- [ ] **Step 3: Implement `TlsHandshake13`**

Implement encoding/parsing with bounded stack buffers, explicit length checks, and no unsupported fallback behavior.

- [ ] **Step 4: Add project entries**

Add new source/header to `.vcxproj`, `.filters`, and host compile source list.

- [ ] **Step 5: Run host TLS tests**

Expected: TLS 1.3 handshake tests pass.

---

## Chunk 3: TLS 1.3 connection flow

### Task 5: Integrate full TLS 1.3 handshake

**Files:**
- Modify: `src/KernelHttp/tls/TlsConnection.h`
- Modify: `src/KernelHttp/tls/TlsConnection.cpp`
- Test: `tests/tls_record_tests.cpp`

- [ ] **Step 1: Add connection option tests where host stubs allow**

Cover option validation, version selection, and TLS 1.3 state transitions with parser-level fixtures.

- [ ] **Step 2: Split existing TLS 1.2 flow into a helper**

Move current `Connect` body into a TLS 1.2 helper while preserving behavior.

- [ ] **Step 3: Implement TLS 1.3 connect helper**

Perform ClientHello, ServerHello/HRR, encrypted handshake, certificate validation, CertificateVerify, Finished verification, client Finished, and application key switch.

- [ ] **Step 4: Handle post-handshake messages**

Consume NewSessionTicket after establishment and reject unsupported post-handshake CertificateRequest.

- [ ] **Step 5: Run host TLS tests**

Expected: TLS 1.2 tests still pass and TLS 1.3 parser/state tests pass.

### Task 6: Add PSK, resumption, and early data

**Files:**
- Modify: `src/KernelHttp/tls/TlsContext.h`
- Modify: `src/KernelHttp/tls/TlsContext.cpp`
- Modify: `src/KernelHttp/tls/TlsHandshake13.h`
- Modify: `src/KernelHttp/tls/TlsHandshake13.cpp`
- Modify: `src/KernelHttp/tls/TlsConnection.cpp`
- Test: `tests/tls_record_tests.cpp`

- [ ] **Step 1: Add failing ticket and binder tests**

Cover NewSessionTicket parsing, binder key derivation, binder placement, and early data accepted/rejected flags.

- [ ] **Step 2: Add session ticket cache structures**

Use fixed-capacity caller-provided storage suitable for kernel code, not dynamic global state.

- [ ] **Step 3: Implement PSK binder and resumption secret flow**

Build ClientHello with placeholder binders, hash the partial ClientHello, compute binder, and patch final binders.

- [ ] **Step 4: Implement 0-RTT send gating**

Allow early data only when ticket permits it, caller explicitly enables it, ALPN/SNI match, and cipher suite matches.

- [ ] **Step 5: Run host TLS tests**

Expected: PSK and early-data tests pass.

---

## Chunk 4: Client options, samples, and integration

### Task 7: Expose TLS 1.3 client options

**Files:**
- Modify: `src/KernelHttp/client/HttpsClient.h`
- Modify: `src/KernelHttp/client/HttpsClient.cpp`

- [ ] **Step 1: Add request options**

Add minimum/maximum TLS version, session resumption, and early data flags.

- [ ] **Step 2: Pass options into TLS**

Populate `TlsClientConnectionOptions` from `HttpsRequestOptions`.

### Task 8: Add samples

**Files:**
- Modify: `src/KernelHttp/samples/HttpVerbSamples.h`
- Modify: `src/KernelHttp/samples/HttpVerbSamples.cpp`

- [ ] **Step 1: Add result fields**

Add TLS 1.3 HTTPS, TLS 1.3 HTTP/2, and TLS 1.3 no-verify fields.

- [ ] **Step 2: Add sample runners**

Add sample request helpers that force TLS 1.3 and log negotiated ALPN.

### Task 9: Add integration script coverage

**Files:**
- Modify: `tests/integration/https_smoke.ps1`

- [ ] **Step 1: Compile TLS 1.3 host source**

Add `src\KernelHttp\tls\TlsHandshake13.cpp` to TLS and HTTP/2 host tests.

- [ ] **Step 2: Force local TLS 1.3 server for smoke mode**

Set Python SSL minimum and maximum version to TLS 1.3 when `-VmSmoke` is used.

- [ ] **Step 3: Run final verification**

Run host tests:

```powershell
pwsh -NoLogo -NoProfile -File tests\integration\https_smoke.ps1 -SkipDriverBuild
```

Run Debug build without artificial timeout:

```powershell
pwsh -NoLogo -NoProfile -File tests\integration\https_smoke.ps1 -Configuration Debug
```

Expected: host regression passes and Debug driver build succeeds.
