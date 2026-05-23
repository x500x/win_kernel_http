#ifndef KERNEL_HTTP_USER_MODE_TEST
#define KERNEL_HTTP_USER_MODE_TEST 1
#endif

#include "../src/KernelHttp/api/KernelHttpApi.h"
#include "../src/KernelHttp/api/KernelHttpWorkspace.h"
#include "../src/KernelHttp/crypto/CngProviderCache.h"

#include <stdio.h>
#include <string.h>

using KernelHttp::api::KH_ASYNC_OPERATION;
using KernelHttp::api::KH_REQUEST;
using KernelHttp::api::KH_RESPONSE;
using KernelHttp::api::KH_SESSION;
using KernelHttp::api::KH_WEBSOCKET;
using KernelHttp::api::KhAsyncCancel;
using KernelHttp::api::KhAsyncRelease;
using KernelHttp::api::KhAsyncWait;
using KernelHttp::api::KhCertificatePolicy;
using KernelHttp::api::KhConnectionPolicy;
using KernelHttp::api::KhDefaultConnectionPoolCapacity;
using KernelHttp::api::KhDefaultConnectionsPerHost;
using KernelHttp::api::KhDefaultIdleTimeoutMilliseconds;
using KernelHttp::api::KhDefaultMaxResponseBytes;
using KernelHttp::api::KhHttpMethod;
using KernelHttp::api::KhHttpRequestCreate;
using KernelHttp::api::KhHttpRequestRelease;
using KernelHttp::api::KhHttpRequestSetBody;
using KernelHttp::api::KhHttpRequestSetConnectionPolicy;
using KernelHttp::api::KhHttpRequestSetHeader;
using KernelHttp::api::KhHttpRequestSetMethod;
using KernelHttp::api::KhHttpRequestSetTlsOptions;
using KernelHttp::api::KhHttpRequestSetUrl;
using KernelHttp::api::KhHttpSendAsync;
using KernelHttp::api::KhHttpSendFlagAggregateWithCallbacks;
using KernelHttp::api::KhHttpSendOptions;
using KernelHttp::api::KhTestSessionHasProviderCache;
using KernelHttp::api::KhTestSessionHasWorkspace;
using KernelHttp::api::KhHttpSendSync;
using KernelHttp::api::KhPoolType;
using KernelHttp::api::KhResponseGetView;
using KernelHttp::api::KhResponseRelease;
using KernelHttp::api::KhResponseView;
using KernelHttp::api::KhSessionClose;
using KernelHttp::api::KhSessionCreate;
using KernelHttp::api::KhSessionOptions;
using KernelHttp::api::KhTestResetCurrentIrql;
using KernelHttp::api::KhTestSetCurrentIrql;
using KernelHttp::api::KhTlsOptions;
using KernelHttp::api::KhTlsVersion;
using KernelHttp::api::KhWebSocketCloseSync;
using KernelHttp::api::KhWebSocketConnectAsync;
using KernelHttp::api::KhWebSocketConnectOptions;
using KernelHttp::api::KhWebSocketConnectSync;
using KernelHttp::api::KhWebSocketMessage;
using KernelHttp::api::KhWebSocketReceiveOptions;
using KernelHttp::api::KhWebSocketReceiveSync;
using KernelHttp::api::KhWebSocketSendBinarySync;
using KernelHttp::api::KhWebSocketSendTextSync;
using KernelHttp::api::KhWebSocketSendOptions;
using KernelHttp::api::KhWorkspace;
using KernelHttp::api::KhWorkspaceAppendResponse;
using KernelHttp::api::KhWorkspaceCertificateScratchBytes;
using KernelHttp::api::KhWorkspaceCreate;
using KernelHttp::api::KhWorkspaceDecodedBodyBytes;
using KernelHttp::api::KhWorkspaceEnsureResponseCapacity;
using KernelHttp::api::KhWorkspaceHttp2HeaderScratchBytes;
using KernelHttp::api::KhWorkspaceOptions;
using KernelHttp::api::KhWorkspaceRelease;
using KernelHttp::api::KhWorkspaceRequestBufferBytes;
using KernelHttp::api::KhWorkspaceReset;
using KernelHttp::api::KhWorkspaceResponseInitialBytes;
using KernelHttp::api::KhWorkspaceTlsHandshakeScratchBytes;
using KernelHttp::api::KhWorkspaceWebSocketFrameScratchBytes;
using KernelHttp::crypto::AesGcmKey;
using KernelHttp::crypto::AesGcmParameters;
using KernelHttp::crypto::CngKey;
using KernelHttp::crypto::CngProvider;
using KernelHttp::crypto::CngProviderCache;
using KernelHttp::crypto::EcCurve;
using KernelHttp::crypto::HashAlgorithm;
using KernelHttp::crypto::SignatureAlgorithm;
using KernelHttp::net::WskClient;

namespace
{
    bool g_failed = false;

    void Expect(bool condition, const char* message)
    {
        if (!condition) {
            g_failed = true;
            printf("FAIL: %s\n", message);
        }
    }

    NTSTATUS TestBodyCallback(void* context, const UCHAR* data, SIZE_T dataLength, bool finalChunk)
    {
        UNREFERENCED_PARAMETER(context);
        UNREFERENCED_PARAMETER(data);
        UNREFERENCED_PARAMETER(dataLength);
        UNREFERENCED_PARAMETER(finalChunk);
        return STATUS_SUCCESS;
    }

    NTSTATUS TestHeaderCallback(
        void* context,
        const char* name,
        SIZE_T nameLength,
        const char* value,
        SIZE_T valueLength)
    {
        UNREFERENCED_PARAMETER(context);
        UNREFERENCED_PARAMETER(name);
        UNREFERENCED_PARAMETER(nameLength);
        UNREFERENCED_PARAMETER(value);
        UNREFERENCED_PARAMETER(valueLength);
        return STATUS_SUCCESS;
    }

    NTSTATUS TestMessageCallback(
        void* context,
        KernelHttp::api::KhWebSocketMessageType type,
        const UCHAR* data,
        SIZE_T dataLength,
        bool finalFragment)
    {
        UNREFERENCED_PARAMETER(context);
        UNREFERENCED_PARAMETER(type);
        UNREFERENCED_PARAMETER(data);
        UNREFERENCED_PARAMETER(dataLength);
        UNREFERENCED_PARAMETER(finalFragment);
        return STATUS_SUCCESS;
    }

    WskClient* FakeWskClient()
    {
        alignas(WskClient) static UCHAR storage[sizeof(WskClient)] = {};
        return reinterpret_cast<WskClient*>(storage);
    }

    KH_SESSION CreateValidSession(WskClient* wskClient)
    {
        KH_SESSION session = nullptr;
        const NTSTATUS status = KhSessionCreate(wskClient, nullptr, &session);
        Expect(status == STATUS_SUCCESS, "session create succeeds with defaults");
        Expect(session != nullptr, "session create returns handle");
        return session;
    }

    KH_REQUEST CreateValidRequest(KH_SESSION session)
    {
        KH_REQUEST request = nullptr;
        NTSTATUS status = KhHttpRequestCreate(session, &request);
        Expect(status == STATUS_SUCCESS, "request create succeeds");
        Expect(request != nullptr, "request create returns handle");

        const char url[] = "https://example.com/path";
        status = KhHttpRequestSetUrl(request, url, strlen(url));
        Expect(status == STATUS_SUCCESS, "request url sets");
        return request;
    }

    void TestDefaultOptions()
    {
        KhSessionOptions sessionOptions = {};
        Expect(sessionOptions.ResponsePoolType == KhPoolType::NonPaged, "default response pool is nonpaged");
        Expect(sessionOptions.MaxResponseBytes == KhDefaultMaxResponseBytes, "default max response bytes is set");
        Expect(sessionOptions.ConnectionPoolCapacity == KhDefaultConnectionPoolCapacity, "default pool capacity is set");
        Expect(sessionOptions.MaxConnectionsPerHost == KhDefaultConnectionsPerHost, "default per-host limit is set");
        Expect(sessionOptions.IdleTimeoutMilliseconds == KhDefaultIdleTimeoutMilliseconds, "default idle timeout is set");
        Expect(sessionOptions.Tls.MinVersion == KhTlsVersion::Tls12, "default min TLS version is 1.2");
        Expect(sessionOptions.Tls.MaxVersion == KhTlsVersion::Tls13, "default max TLS version is 1.3");
        Expect(sessionOptions.Tls.CertificatePolicy == KhCertificatePolicy::Verify, "default certificate policy verifies");

        KhHttpSendOptions sendOptions = {};
        Expect(sendOptions.MaxResponseBytes == 0, "send options inherit max response by default");
        Expect(sendOptions.HeaderCallback == nullptr, "send options header callback is null by default");
        Expect(sendOptions.BodyCallback == nullptr, "send options body callback is null by default");

        KhWebSocketConnectOptions websocketOptions = {};
        Expect(websocketOptions.MaxMessageBytes == KhDefaultMaxResponseBytes, "websocket connect max defaults");
        Expect(websocketOptions.AutoReplyPing, "websocket connect auto-replies ping by default");
    }

    void TestSessionValidation()
    {
        WskClient* wskClient = FakeWskClient();
        KH_SESSION session = nullptr;

        NTSTATUS status = KhSessionCreate(nullptr, nullptr, &session);
        Expect(status == STATUS_INVALID_PARAMETER, "session create rejects null WSK client");
        Expect(session == nullptr, "session remains null after null WSK client");

        status = KhSessionCreate(wskClient, nullptr, nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "session create rejects null output");

        KhSessionOptions options = {};
        options.MaxResponseBytes = 0;
        status = KhSessionCreate(wskClient, &options, &session);
        Expect(status == STATUS_INVALID_PARAMETER, "session create rejects zero max response bytes");

        options = {};
        options.ConnectionPoolCapacity = 0;
        status = KhSessionCreate(wskClient, &options, &session);
        Expect(status == STATUS_INVALID_PARAMETER, "session create rejects zero pool capacity");

        options = {};
        options.Tls.MinVersion = KhTlsVersion::Tls13;
        options.Tls.MaxVersion = KhTlsVersion::Tls12;
        status = KhSessionCreate(wskClient, &options, &session);
        Expect(status == STATUS_INVALID_PARAMETER, "session create rejects reversed TLS range");

        session = CreateValidSession(wskClient);
        Expect(KhTestSessionHasWorkspace(session), "session create initializes workspace");
        Expect(KhTestSessionHasProviderCache(session), "session create initializes provider cache");
        KhSessionClose(session);
        KhSessionClose(nullptr);
    }

    void TestWorkspaceLifecycle()
    {
        KhWorkspace* workspace = reinterpret_cast<KhWorkspace*>(static_cast<size_t>(1));
        NTSTATUS status = KhWorkspaceCreate(nullptr, nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "workspace create rejects null output");

        KhWorkspaceOptions options = {};
        options.MaxResponseBytes = 0;
        status = KhWorkspaceCreate(&options, &workspace);
        Expect(status == STATUS_INVALID_PARAMETER, "workspace create rejects zero max response");
        Expect(workspace == nullptr, "workspace create clears output on invalid options");

        options = {};
        options.PoolType = KhPoolType::Paged;
        options.MaxResponseBytes = KhWorkspaceResponseInitialBytes + 64;
        status = KhWorkspaceCreate(&options, &workspace);
        Expect(status == STATUS_SUCCESS, "workspace create accepts paged option");
        Expect(workspace != nullptr, "workspace create returns object");
        Expect(workspace->PoolType == KhPoolType::Paged, "workspace records paged pool selection");
        Expect(workspace->MaxResponseBytes == options.MaxResponseBytes, "workspace records response limit");
        Expect(workspace->Request.Length == KhWorkspaceRequestBufferBytes, "workspace request buffer size is fixed");
        Expect(workspace->Response.Length == KhWorkspaceResponseInitialBytes, "workspace response starts at fixed initial size");
        Expect(workspace->DecodedBody.Length == KhWorkspaceDecodedBodyBytes, "workspace decoded body size is fixed");
        Expect(workspace->Http2HeaderScratch.Length == KhWorkspaceHttp2HeaderScratchBytes, "workspace http2 scratch size is fixed");
        Expect(workspace->TlsHandshakeScratch.Length == KhWorkspaceTlsHandshakeScratchBytes, "workspace tls scratch size is fixed");
        Expect(workspace->CertificateScratch.Length == KhWorkspaceCertificateScratchBytes, "workspace cert scratch size is fixed");
        Expect(workspace->WebSocketFrameScratch.Length == KhWorkspaceWebSocketFrameScratchBytes, "workspace websocket scratch size is fixed");

        const UCHAR payload[] = { 'a', 'b', 'c', 'd' };
        status = KhWorkspaceAppendResponse(workspace, payload, sizeof(payload));
        Expect(status == STATUS_SUCCESS, "workspace appends response data");
        Expect(workspace->ResponseLength == sizeof(payload), "workspace tracks response length");
        Expect(memcmp(workspace->Response.Data, payload, sizeof(payload)) == 0, "workspace copies response bytes");

        status = KhWorkspaceEnsureResponseCapacity(workspace, KhWorkspaceResponseInitialBytes + 1);
        Expect(status == STATUS_SUCCESS, "workspace grows response within max");
        Expect(workspace->Response.Length >= KhWorkspaceResponseInitialBytes + 1, "workspace response grew");

        status = KhWorkspaceEnsureResponseCapacity(workspace, options.MaxResponseBytes + 1);
        Expect(status == STATUS_BUFFER_TOO_SMALL, "workspace rejects oversized response grow");
        Expect(workspace->ResponseLength == sizeof(payload), "workspace keeps response length after rejected grow");

        KhWorkspaceReset(workspace);
        Expect(workspace->ResponseLength == 0, "workspace reset clears response length");
        Expect(workspace->Response.Data[0] == 0, "workspace reset clears response bytes");

        KhWorkspaceRelease(workspace);
        KhWorkspaceRelease(nullptr);
    }

    void TestProviderCache()
    {
        CngProviderCache cache;
        Expect(!cache.IsInitialized(), "provider cache starts uninitialized");
        NTSTATUS status = cache.Initialize();
        Expect(status == STATUS_SUCCESS, "provider cache initializes");
        Expect(cache.IsInitialized(), "provider cache reports initialized");
        Expect(cache.Aes() != nullptr, "provider cache has AES provider");
        Expect(cache.Hash(HashAlgorithm::Sha1) != nullptr, "provider cache has SHA1 provider");
        Expect(cache.Hash(HashAlgorithm::Sha256) != nullptr, "provider cache has SHA256 provider");
        Expect(cache.Hash(HashAlgorithm::Sha384) != nullptr, "provider cache has SHA384 provider");
        Expect(cache.Hmac(HashAlgorithm::Sha256) != nullptr, "provider cache has HMAC provider");
        Expect(cache.Rsa() != nullptr, "provider cache has RSA provider");
        Expect(cache.Ecdsa(EcCurve::P256) != nullptr, "provider cache has ECDSA provider");
        Expect(cache.Ecdh(EcCurve::P256) != nullptr, "provider cache has ECDH provider");

        UCHAR output[64] = {};
        SIZE_T bytesWritten = 0;
        const UCHAR data[] = { 1, 2, 3 };
        const UCHAR key[] = { 4, 5, 6, 7 };
        const ULONG initialUseCount = cache.CachedProviderUseCountForTest();

        status = CngProvider::Hash(&cache, HashAlgorithm::Sha256, data, sizeof(data), output, sizeof(output), &bytesWritten);
        Expect(status == STATUS_SUCCESS, "cached hash succeeds");
        Expect(bytesWritten == 32, "cached hash writes digest length");

        status = CngProvider::Hmac(&cache, HashAlgorithm::Sha256, key, sizeof(key), data, sizeof(data), output, sizeof(output), &bytesWritten);
        Expect(status == STATUS_SUCCESS, "cached hmac succeeds");

        UCHAR ciphertext[sizeof(data)] = {};
        UCHAR plaintext[sizeof(data)] = {};
        UCHAR tag[16] = {};
        const UCHAR nonce[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        AesGcmKey aesKey = { key, sizeof(key) };
        AesGcmParameters encryptParams = {};
        encryptParams.Nonce.Data = nonce;
        encryptParams.Nonce.Length = sizeof(nonce);
        status = CngProvider::AesGcmEncrypt(
            &cache,
            aesKey,
            encryptParams,
            data,
            sizeof(data),
            ciphertext,
            sizeof(ciphertext),
            tag,
            sizeof(tag),
            &bytesWritten);
        Expect(status == STATUS_SUCCESS, "cached AES-GCM encrypt succeeds");

        AesGcmParameters decryptParams = encryptParams;
        decryptParams.Tag.Data = tag;
        decryptParams.Tag.Length = sizeof(tag);
        status = CngProvider::AesGcmDecrypt(
            &cache,
            aesKey,
            decryptParams,
            ciphertext,
            sizeof(ciphertext),
            plaintext,
            sizeof(plaintext),
            &bytesWritten);
        Expect(status == STATUS_SUCCESS, "cached AES-GCM decrypt succeeds");

        CngKey privateKey;
        CngKey ecdhPublicKey;
        CngKey ecdsaPublicKey;
        CngKey rsaPublicKey;
        UCHAR point[65] = {};
        point[0] = 4;
        status = CngProvider::GenerateEcdhKeyPair(&cache, EcCurve::P256, privateKey);
        Expect(status == STATUS_SUCCESS, "cached ECDH key generation succeeds");
        status = CngProvider::ImportEcdhPublicKey(&cache, EcCurve::P256, point, sizeof(point), ecdhPublicKey);
        Expect(status == STATUS_SUCCESS, "cached ECDH public import succeeds");
        status = CngProvider::ImportEcdsaPublicKey(&cache, EcCurve::P256, point, sizeof(point), ecdsaPublicKey);
        Expect(status == STATUS_SUCCESS, "cached ECDSA public import succeeds");

        const UCHAR exponent[] = { 1, 0, 1 };
        const UCHAR modulus[] = { 0xA1, 0xB2, 0xC3, 0xD4 };
        status = CngProvider::ImportRsaPublicKey(&cache, exponent, sizeof(exponent), modulus, sizeof(modulus), rsaPublicKey);
        Expect(status == STATUS_SUCCESS, "cached RSA public import succeeds");

        status = CngProvider::VerifySignature(
            &cache,
            SignatureAlgorithm::EcdsaSha256,
            ecdsaPublicKey,
            output,
            32,
            data,
            sizeof(data));
        Expect(status == STATUS_SUCCESS, "cached signature helper succeeds");

        UCHAR secret[32] = {};
        status = CngProvider::DeriveEcdhSecret(&cache, privateKey, ecdhPublicKey, secret, sizeof(secret), &bytesWritten);
        Expect(status == STATUS_SUCCESS, "cached ECDH secret derive succeeds");

        Expect(cache.CachedProviderUseCountForTest() > initialUseCount, "cached provider paths are selected");
        cache.Shutdown();
        Expect(!cache.IsInitialized(), "provider cache shutdown clears initialized state");
    }

    void TestRequestValidation()
    {
        WskClient* wskClient = FakeWskClient();
        KH_SESSION session = CreateValidSession(wskClient);
        KH_REQUEST request = nullptr;

        NTSTATUS status = KhHttpRequestCreate(nullptr, &request);
        Expect(status == STATUS_INVALID_PARAMETER, "request create rejects null session");
        status = KhHttpRequestCreate(session, nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "request create rejects null output");

        status = KhHttpRequestCreate(session, &request);
        Expect(status == STATUS_SUCCESS, "request create succeeds for setter tests");

        const char url[] = "https://example.com/";
        status = KhHttpRequestSetUrl(nullptr, url, strlen(url));
        Expect(status == STATUS_INVALID_PARAMETER, "set url rejects null request");
        status = KhHttpRequestSetUrl(request, nullptr, strlen(url));
        Expect(status == STATUS_INVALID_PARAMETER, "set url rejects null url");
        status = KhHttpRequestSetUrl(request, url, 0);
        Expect(status == STATUS_INVALID_PARAMETER, "set url rejects empty url");
        status = KhHttpRequestSetUrl(request, url, strlen(url));
        Expect(status == STATUS_SUCCESS, "set url accepts valid url");

        status = KhHttpRequestSetMethod(request, static_cast<KhHttpMethod>(99));
        Expect(status == STATUS_INVALID_PARAMETER, "set method rejects unknown method");
        status = KhHttpRequestSetMethod(request, KhHttpMethod::Post);
        Expect(status == STATUS_SUCCESS, "set method accepts POST");

        status = KhHttpRequestSetHeader(request, nullptr, 4, "x", 1);
        Expect(status == STATUS_INVALID_PARAMETER, "set header rejects null name");
        status = KhHttpRequestSetHeader(request, "Name", 0, "x", 1);
        Expect(status == STATUS_INVALID_PARAMETER, "set header rejects empty name");
        status = KhHttpRequestSetHeader(request, "Name", 4, nullptr, 0);
        Expect(status == STATUS_INVALID_PARAMETER, "set header rejects null value");
        status = KhHttpRequestSetHeader(request, "Name", 4, "Value", 5);
        Expect(status == STATUS_NOT_SUPPORTED, "set header is explicit not-supported until header store chunk");

        const UCHAR body[] = { 'o', 'k' };
        status = KhHttpRequestSetBody(request, nullptr, sizeof(body));
        Expect(status == STATUS_INVALID_PARAMETER, "set body rejects null non-empty body");
        status = KhHttpRequestSetBody(request, body, sizeof(body));
        Expect(status == STATUS_SUCCESS, "set body accepts valid body");

        KhTlsOptions tlsOptions = {};
        tlsOptions.MinVersion = KhTlsVersion::Tls13;
        tlsOptions.MaxVersion = KhTlsVersion::Tls12;
        status = KhHttpRequestSetTlsOptions(request, &tlsOptions);
        Expect(status == STATUS_INVALID_PARAMETER, "set TLS rejects reversed range");
        tlsOptions = {};
        status = KhHttpRequestSetTlsOptions(request, &tlsOptions);
        Expect(status == STATUS_SUCCESS, "set TLS accepts defaults");

        status = KhHttpRequestSetConnectionPolicy(request, static_cast<KhConnectionPolicy>(99));
        Expect(status == STATUS_INVALID_PARAMETER, "set connection policy rejects unknown policy");
        status = KhHttpRequestSetConnectionPolicy(request, KhConnectionPolicy::ForceNew);
        Expect(status == STATUS_SUCCESS, "set connection policy accepts force-new");

        KhHttpRequestRelease(request);
        KhSessionClose(session);
    }

    void TestHttpSendValidation()
    {
        WskClient* wskClient = FakeWskClient();
        KH_SESSION session = CreateValidSession(wskClient);
        KH_REQUEST request = nullptr;

        NTSTATUS status = KhHttpRequestCreate(session, &request);
        Expect(status == STATUS_SUCCESS, "request create succeeds for send validation");

        KH_RESPONSE response = reinterpret_cast<KH_RESPONSE>(static_cast<size_t>(1));
        status = KhHttpSendSync(session, request, nullptr, &response);
        Expect(status == STATUS_INVALID_PARAMETER, "send sync rejects request without URL");
        Expect(response == nullptr, "send sync clears response output on validation failure");

        const char url[] = "https://example.com/";
        status = KhHttpRequestSetUrl(request, url, strlen(url));
        Expect(status == STATUS_SUCCESS, "send validation request URL sets");

        status = KhHttpSendSync(nullptr, request, nullptr, &response);
        Expect(status == STATUS_INVALID_PARAMETER, "send sync rejects null session");
        status = KhHttpSendSync(session, nullptr, nullptr, &response);
        Expect(status == STATUS_INVALID_PARAMETER, "send sync rejects null request");

        KhHttpSendOptions options = {};
        options.MaxResponseBytes = 0;
        options.CallbackContext = &options;
        status = KhHttpSendSync(session, request, &options, &response);
        Expect(status == STATUS_INVALID_PARAMETER, "send sync rejects callback context without callbacks");

        options = {};
        options.Flags = KhHttpSendFlagAggregateWithCallbacks;
        status = KhHttpSendSync(session, request, &options, &response);
        Expect(status == STATUS_INVALID_PARAMETER, "send sync rejects aggregate-with-callbacks without body callback");

        options.BodyCallback = TestBodyCallback;
        status = KhHttpSendSync(session, request, &options, nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "send sync rejects aggregate callback mode without response output");

        options = {};
        options.BodyCallback = TestBodyCallback;
        options.CallbackContext = &options;
        status = KhHttpSendSync(session, request, &options, nullptr);
        Expect(status == STATUS_NOT_SUPPORTED, "send sync validates callback-only mode before transport chunk");

        options = {};
        options.HeaderCallback = TestHeaderCallback;
        status = KhHttpSendSync(session, request, &options, nullptr);
        Expect(status == STATUS_NOT_SUPPORTED, "send sync validates header callback mode before transport chunk");

        KH_ASYNC_OPERATION operation = reinterpret_cast<KH_ASYNC_OPERATION>(static_cast<size_t>(1));
        status = KhHttpSendAsync(session, request, nullptr, nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "send async rejects null operation output");
        status = KhHttpSendAsync(session, request, nullptr, &operation);
        Expect(status == STATUS_NOT_SUPPORTED, "send async validates inputs before async worker chunk");
        Expect(operation == nullptr, "send async clears operation output before not-supported");

        KhHttpRequestRelease(request);
        KhSessionClose(session);
    }

    void TestResponseValidation()
    {
        KhResponseView view = {};
        NTSTATUS status = KhResponseGetView(nullptr, &view);
        Expect(status == STATUS_INVALID_PARAMETER, "response view rejects null response");
        status = KhResponseGetView(nullptr, nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "response view rejects null output");
        KhResponseRelease(nullptr);
    }

    void TestWebSocketValidation()
    {
        WskClient* wskClient = FakeWskClient();
        KH_SESSION session = CreateValidSession(wskClient);
        KH_WEBSOCKET websocket = nullptr;
        KhWebSocketConnectOptions connectOptions = {};

        NTSTATUS status = KhWebSocketConnectSync(nullptr, &connectOptions, &websocket);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket connect rejects null session");
        status = KhWebSocketConnectSync(session, nullptr, &websocket);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket connect rejects null options");
        status = KhWebSocketConnectSync(session, &connectOptions, &websocket);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket connect rejects missing URL");

        const char url[] = "wss://example.com/socket";
        connectOptions.Url = url;
        connectOptions.UrlLength = strlen(url);
        connectOptions.MaxMessageBytes = 0;
        status = KhWebSocketConnectSync(session, &connectOptions, &websocket);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket connect rejects zero max message bytes");

        connectOptions.MaxMessageBytes = 4096;
        status = KhWebSocketConnectSync(session, &connectOptions, nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket connect rejects null output");
        status = KhWebSocketConnectSync(session, &connectOptions, &websocket);
        Expect(status == STATUS_NOT_SUPPORTED, "websocket connect validates before transport chunk");

        KH_ASYNC_OPERATION operation = reinterpret_cast<KH_ASYNC_OPERATION>(static_cast<size_t>(1));
        status = KhWebSocketConnectAsync(session, &connectOptions, nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket connect async rejects null operation output");
        status = KhWebSocketConnectAsync(session, &connectOptions, &operation);
        Expect(status == STATUS_NOT_SUPPORTED, "websocket connect async validates before worker chunk");
        Expect(operation == nullptr, "websocket connect async clears operation output before not-supported");

        const char text[] = "hello";
        KhWebSocketSendOptions sendOptions = {};
        status = KhWebSocketSendTextSync(nullptr, text, strlen(text), &sendOptions);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket send text rejects null websocket");

        const UCHAR data[] = { 1, 2, 3 };
        status = KhWebSocketSendBinarySync(nullptr, data, sizeof(data), &sendOptions);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket send binary rejects null websocket");

        KhWebSocketReceiveOptions receiveOptions = {};
        KhWebSocketMessage message = {};
        receiveOptions.AutoAllocate = false;
        receiveOptions.MessageCallback = TestMessageCallback;
        status = KhWebSocketReceiveSync(nullptr, &receiveOptions, &message);
        Expect(status == STATUS_INVALID_PARAMETER, "websocket receive rejects null websocket");

        status = KhWebSocketCloseSync(nullptr);
        Expect(status == STATUS_SUCCESS, "websocket close accepts null");

        KhSessionClose(session);
    }

    void TestAsyncValidation()
    {
        NTSTATUS status = KhAsyncCancel(nullptr);
        Expect(status == STATUS_INVALID_PARAMETER, "async cancel rejects null operation");
        status = KhAsyncWait(nullptr, 0);
        Expect(status == STATUS_INVALID_PARAMETER, "async wait rejects null operation");
        KhAsyncRelease(nullptr);
    }

    void TestIrqlGuards()
    {
        WskClient* wskClient = FakeWskClient();
        KH_SESSION session = nullptr;
        KH_REQUEST request = reinterpret_cast<KH_REQUEST>(static_cast<size_t>(1));
        KH_RESPONSE response = reinterpret_cast<KH_RESPONSE>(static_cast<size_t>(1));
        KH_WEBSOCKET websocket = reinterpret_cast<KH_WEBSOCKET>(static_cast<size_t>(1));
        KH_ASYNC_OPERATION operation = reinterpret_cast<KH_ASYNC_OPERATION>(static_cast<size_t>(1));
        KhResponseView responseView = {};
        KhWebSocketMessage message = {};
        const char text[] = "x";
        const UCHAR data[] = { 1 };

        KhTestSetCurrentIrql(2);
        NTSTATUS status = KhSessionCreate(wskClient, nullptr, &session);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "session create fails first at raised IRQL");
        Expect(session == nullptr, "session create does not touch output at raised IRQL");

        status = KhHttpRequestCreate(nullptr, &request);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "request create fails first at raised IRQL");
        Expect(request == reinterpret_cast<KH_REQUEST>(static_cast<size_t>(1)), "request create does not touch output at raised IRQL");

        status = KhHttpSendSync(nullptr, nullptr, nullptr, &response);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "send sync fails first at raised IRQL");
        Expect(response == reinterpret_cast<KH_RESPONSE>(static_cast<size_t>(1)), "send sync does not touch output at raised IRQL");

        status = KhHttpSendAsync(nullptr, nullptr, nullptr, &operation);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "send async fails first at raised IRQL");
        Expect(operation == reinterpret_cast<KH_ASYNC_OPERATION>(static_cast<size_t>(1)), "send async does not touch output at raised IRQL");

        status = KhResponseGetView(nullptr, &responseView);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "response get view fails first at raised IRQL");

        status = KhWebSocketConnectSync(nullptr, nullptr, &websocket);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "websocket connect sync fails first at raised IRQL");
        Expect(websocket == reinterpret_cast<KH_WEBSOCKET>(static_cast<size_t>(1)), "websocket connect sync does not touch output at raised IRQL");

        status = KhWebSocketConnectAsync(nullptr, nullptr, &operation);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "websocket connect async fails first at raised IRQL");

        status = KhWebSocketSendTextSync(nullptr, text, strlen(text), nullptr);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "websocket send text fails first at raised IRQL");
        status = KhWebSocketSendBinarySync(nullptr, data, sizeof(data), nullptr);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "websocket send binary fails first at raised IRQL");
        status = KhWebSocketReceiveSync(nullptr, nullptr, &message);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "websocket receive fails first at raised IRQL");
        status = KhWebSocketCloseSync(nullptr);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "websocket close fails first at raised IRQL");

        status = KhAsyncCancel(nullptr);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "async cancel fails first at raised IRQL");
        status = KhAsyncWait(nullptr, 0);
        Expect(status == STATUS_INVALID_DEVICE_REQUEST, "async wait fails first at raised IRQL");

        KhSessionClose(nullptr);
        KhHttpRequestRelease(nullptr);
        KhResponseRelease(nullptr);
        KhAsyncRelease(nullptr);

        KhTestResetCurrentIrql();
    }
}

int main()
{
    TestDefaultOptions();
    TestSessionValidation();
    TestWorkspaceLifecycle();
    TestProviderCache();
    TestRequestValidation();
    TestHttpSendValidation();
    TestResponseValidation();
    TestWebSocketValidation();
    TestAsyncValidation();
    TestIrqlGuards();

    if (g_failed) {
        return 1;
    }

    printf("high level api tests passed\n");
    return 0;
}
