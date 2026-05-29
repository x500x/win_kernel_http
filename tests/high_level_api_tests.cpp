#ifndef KERNEL_HTTP_USER_MODE_TEST
#define KERNEL_HTTP_USER_MODE_TEST 1
#endif

#include "../src/KernelHttp/khttp/Test.h"
#include "../src/KernelHttp/samples/HighLevelApiSamples.h"

#include <stdio.h>

namespace
{
    bool g_failed = false;

    void Expect(bool condition, const char* message) noexcept
    {
        if (!condition) {
            g_failed = true;
            printf("FAIL: %s\n", message);
        }
    }

    bool TextEqualsLiteral(const char* value, SIZE_T valueLength, const char* literal) noexcept
    {
        if (value == nullptr || literal == nullptr) {
            return false;
        }

        for (SIZE_T index = 0; index < valueLength; ++index) {
            if (literal[index] == '\0' || value[index] != literal[index]) {
                return false;
            }
        }
        return literal[valueLength] == '\0';
    }

    struct SampleCapture final
    {
        SIZE_T HttpCalls = 0;
        SIZE_T HttpIpv4Calls = 0;
        SIZE_T HttpAnyCalls = 0;
        SIZE_T HttpReuseCalls = 0;
        SIZE_T HttpNoPoolCalls = 0;
        SIZE_T HttpForceNewCalls = 0;
        SIZE_T HttpsVerifyCalls = 0;
        SIZE_T HttpsVerifyWithStoreCalls = 0;
        SIZE_T HttpsNoVerifyCalls = 0;
        SIZE_T HttpsNoVerifyWithoutStoreCalls = 0;
        SIZE_T WebSocketConnectCalls = 0;
        SIZE_T WebSocketIpv4Calls = 0;
        SIZE_T WebSocketAnyCalls = 0;
        SIZE_T WebSocketPlainCalls = 0;
        SIZE_T WebSocketSecureCalls = 0;
        SIZE_T WebSocketVerifyCalls = 0;
        SIZE_T WebSocketVerifyWithStoreCalls = 0;
        SIZE_T WebSocketTls12MaxCalls = 0;
        SIZE_T WebSocketSendCalls = 0;
        SIZE_T WebSocketTextSendCalls = 0;
        SIZE_T WebSocketBinarySendCalls = 0;
        SIZE_T WebSocketNonFinalSendCalls = 0;
        SIZE_T WebSocketReceiveCalls = 0;
        SIZE_T WebSocketCloseCalls = 0;
        UCHAR WebSocketEcho[32] = {};
        SIZE_T WebSocketEchoLength = 0;
    };

    NTSTATUS HttpTransport(
        void* context,
        const KernelHttp::engine::KhTestHttpTransportRequest* request,
        KernelHttp::engine::KhTestHttpTransportResponse* response) noexcept
    {
        auto* capture = static_cast<SampleCapture*>(context);
        if (capture == nullptr || request == nullptr || response == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        ++capture->HttpCalls;
        if (request->AddressFamily == KernelHttp::engine::KhAddressFamily::Ipv4) {
            ++capture->HttpIpv4Calls;
        }
        if (request->AddressFamily == KernelHttp::engine::KhAddressFamily::Any) {
            ++capture->HttpAnyCalls;
        }
        if (request->ConnectionPolicy == KernelHttp::engine::KhConnectionPolicy::ReuseOrCreate) {
            ++capture->HttpReuseCalls;
        }
        if (request->ConnectionPolicy == KernelHttp::engine::KhConnectionPolicy::NoPool) {
            ++capture->HttpNoPoolCalls;
        }
        if (request->ConnectionPolicy == KernelHttp::engine::KhConnectionPolicy::ForceNew) {
            ++capture->HttpForceNewCalls;
        }

        const bool isHttps = TextEqualsLiteral(request->Scheme, request->SchemeLength, "https");
        if (isHttps &&
            request->CertificatePolicy == KernelHttp::engine::KhCertificatePolicy::Verify) {
            ++capture->HttpsVerifyCalls;
            if (request->CertificateStore != nullptr) {
                ++capture->HttpsVerifyWithStoreCalls;
            }
        }
        if (isHttps &&
            request->CertificatePolicy == KernelHttp::engine::KhCertificatePolicy::NoVerify) {
            ++capture->HttpsNoVerifyCalls;
            if (request->CertificateStore == nullptr) {
                ++capture->HttpsNoVerifyWithoutStoreCalls;
            }
        }

        static const char rawResponse[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 0\r\n"
            "X-KernelHttp-Test: high-level\r\n"
            "Connection: close\r\n"
            "\r\n";
        response->RawResponse = rawResponse;
        response->RawResponseLength = sizeof(rawResponse) - 1;
        response->ConnectionReusable = false;
        return STATUS_SUCCESS;
    }

    NTSTATUS WebSocketConnect(
        void* context,
        const KernelHttp::engine::KhTestWebSocketConnectRequest* request) noexcept
    {
        auto* capture = static_cast<SampleCapture*>(context);
        if (capture == nullptr || request == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        ++capture->WebSocketConnectCalls;
        if (request->AddressFamily == KernelHttp::engine::KhAddressFamily::Ipv4) {
            ++capture->WebSocketIpv4Calls;
        }
        if (request->AddressFamily == KernelHttp::engine::KhAddressFamily::Any) {
            ++capture->WebSocketAnyCalls;
        }
        if (TextEqualsLiteral(request->Scheme, request->SchemeLength, "ws")) {
            ++capture->WebSocketPlainCalls;
        }
        if (TextEqualsLiteral(request->Scheme, request->SchemeLength, "wss")) {
            ++capture->WebSocketSecureCalls;
        }
        if (TextEqualsLiteral(request->Scheme, request->SchemeLength, "wss") &&
            request->CertificatePolicy == KernelHttp::engine::KhCertificatePolicy::Verify) {
            ++capture->WebSocketVerifyCalls;
            if (request->CertificateStore != nullptr) {
                ++capture->WebSocketVerifyWithStoreCalls;
            }
            if (request->MaxTlsVersion == KernelHttp::engine::KhTlsVersion::Tls12) {
                ++capture->WebSocketTls12MaxCalls;
            }
        }
        return STATUS_SUCCESS;
    }

    NTSTATUS WebSocketSend(
        void* context,
        KernelHttp::engine::KH_WEBSOCKET websocket,
        KernelHttp::engine::KhWebSocketMessageType type,
        const UCHAR* data,
        SIZE_T dataLength,
        bool finalFragment) noexcept
    {
        UNREFERENCED_PARAMETER(websocket);
        UNREFERENCED_PARAMETER(data);
        UNREFERENCED_PARAMETER(dataLength);

        auto* capture = static_cast<SampleCapture*>(context);
        if (capture == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        ++capture->WebSocketSendCalls;
        if (type == KernelHttp::engine::KhWebSocketMessageType::Text) {
            ++capture->WebSocketTextSendCalls;
        }
        if (type == KernelHttp::engine::KhWebSocketMessageType::Binary) {
            ++capture->WebSocketBinarySendCalls;
        }
        if (!finalFragment) {
            ++capture->WebSocketNonFinalSendCalls;
        }
        return STATUS_SUCCESS;
    }

    NTSTATUS WebSocketReceive(
        void* context,
        KernelHttp::engine::KH_WEBSOCKET websocket,
        KernelHttp::engine::KhTestWebSocketMessage* message) noexcept
    {
        UNREFERENCED_PARAMETER(websocket);
        auto* capture = static_cast<SampleCapture*>(context);
        if (capture == nullptr || message == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        ++capture->WebSocketReceiveCalls;
        message->Type = KernelHttp::engine::KhWebSocketMessageType::Text;
        message->Data = capture->WebSocketEcho;
        message->DataLength = capture->WebSocketEchoLength;
        message->FinalFragment = true;
        return STATUS_SUCCESS;
    }

    void WebSocketClose(void* context, KernelHttp::engine::KH_WEBSOCKET websocket) noexcept
    {
        UNREFERENCED_PARAMETER(websocket);
        auto* capture = static_cast<SampleCapture*>(context);
        if (capture != nullptr) {
            ++capture->WebSocketCloseCalls;
        }
    }

    void TestLoadTimeSamplesCoverHighLevelSurface() noexcept
    {
        SampleCapture capture = {};
        static const char echo[] = "hello-from-khttp";
        capture.WebSocketEchoLength = sizeof(echo) - 1;
        for (SIZE_T index = 0; index < capture.WebSocketEchoLength; ++index) {
            capture.WebSocketEcho[index] = static_cast<UCHAR>(echo[index]);
        }

        KernelHttp::khttp::test::SetAsyncAutoRun(true);
        KernelHttp::khttp::test::SetHttpTransport(HttpTransport, &capture);
        KernelHttp::khttp::test::SetWebSocketTransport(
            WebSocketConnect,
            WebSocketSend,
            WebSocketReceive,
            WebSocketClose,
            &capture);

        KernelHttp::samples::HighLevelApiSampleResults results = {};
        NTSTATUS status = KernelHttp::samples::RunHighLevelApiSamples(
            reinterpret_cast<KernelHttp::net::WskClient*>(0x1),
            &results);

        Expect(NT_SUCCESS(status), "load-time high-level samples succeed under test transport");
        Expect(NT_SUCCESS(results.SessionDefaultConfig.Status), "default session sample succeeds");
        Expect(NT_SUCCESS(results.SessionCustomConfig.Status), "custom session sample succeeds");
        Expect(capture.HttpCalls == 33, "all HTTP/HTTPS high-level samples are issued");
        Expect(capture.HttpIpv4Calls == 23, "request-builder HTTP/HTTPS samples force IPv4");
        Expect(capture.HttpAnyCalls == 10, "shortcut HTTP/async samples use default address family");
        Expect(capture.HttpNoPoolCalls >= 11, "no-pool connection policy samples are issued");
        Expect(capture.HttpForceNewCalls >= 1, "force-new connection policy sample is issued");
        Expect(capture.HttpsVerifyCalls == 2, "verified HTTPS samples are issued");
        Expect(
            capture.HttpsVerifyWithStoreCalls == capture.HttpsVerifyCalls,
            "verified HTTPS samples provide a certificate store");
        Expect(capture.HttpsNoVerifyCalls == 1, "no-verify HTTPS sample is issued");
        Expect(
            capture.HttpsNoVerifyWithoutStoreCalls == capture.HttpsNoVerifyCalls,
            "no-verify HTTPS sample does not require a certificate store");

        Expect(capture.WebSocketConnectCalls == 10, "all websocket connect variants are issued");
        Expect(capture.WebSocketIpv4Calls == 8, "configured websocket samples force IPv4");
        Expect(capture.WebSocketAnyCalls == 2, "URL websocket samples use default address family");
        Expect(capture.WebSocketPlainCalls == 2, "plain ws URL samples are issued");
        Expect(capture.WebSocketSecureCalls == 8, "secure wss samples are issued");
        Expect(capture.WebSocketVerifyCalls == 8, "verified websocket samples are issued");
        Expect(capture.WebSocketVerifyWithStoreCalls == 8, "verified websocket samples provide a certificate store");
        Expect(capture.WebSocketTls12MaxCalls == 8, "websocket secure samples cap TLS at 1.2 for endpoint compatibility");
        Expect(capture.WebSocketSendCalls == 7, "websocket send variants are issued");
        Expect(capture.WebSocketTextSendCalls == 5, "websocket text send variants are issued");
        Expect(capture.WebSocketBinarySendCalls == 2, "websocket binary send variants are issued");
        Expect(capture.WebSocketNonFinalSendCalls == 2, "websocket Ex send options are issued");
        Expect(capture.WebSocketReceiveCalls == 7, "websocket receive variants are issued");
        Expect(capture.WebSocketCloseCalls == 10, "each websocket connect path closes its handle");
        Expect(results.WebSocketEcho.BodyLength == capture.WebSocketEchoLength, "websocket echo sample receives body");
        Expect(results.WebSocketReceiveEx.BodyLength == capture.WebSocketEchoLength, "websocket receive callback records body");
        Expect(results.HttpAsyncCancel.StatusCode == 1, "async cancel sample marks operation canceled");

        KernelHttp::khttp::test::SetHttpTransport(nullptr, nullptr);
        KernelHttp::khttp::test::SetWebSocketTransport(nullptr, nullptr, nullptr, nullptr, nullptr);
    }
}

int main() noexcept
{
    KernelHttp::khttp::test::ResetCurrentIrql();
    TestLoadTimeSamplesCoverHighLevelSurface();

    if (g_failed) {
        printf("high-level API tests FAILED\n");
        return 1;
    }

    printf("high-level API tests passed\n");
    return 0;
}
