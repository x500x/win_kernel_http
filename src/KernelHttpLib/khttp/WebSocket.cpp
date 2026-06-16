#include <KernelHttp/khttp/WebSocket.h>
#include <KernelHttp/khttp/Detail.h>
#include <KernelHttp/engine/Engine.h>

namespace KernelHttp
{
namespace kwebsocket
{
namespace
{
    void FillApiConnectOptions(
        const ConnectConfig& src,
        engine::KhWebSocketConnectOptions& dst) noexcept
    {
        dst.Url = src.Url;
        dst.UrlLength = src.UrlLength;
        dst.Subprotocol = src.Subprotocol;
        dst.SubprotocolLength = src.SubprotocolLength;
        khttp::detail::FillApiTlsOptions(src.Tls, dst.Tls);
        dst.AddressFamily = khttp::detail::ToApiAddressFamily(src.Family);
        dst.MaxMessageBytes = src.MaxMessageBytes;
        dst.AutoReplyPing = src.AutoReplyPing;
    }
}

NTSTATUS Connect(khttp::Session* session, const char* url, SIZE_T urlLength, WebSocket** websocket) noexcept
{
    if (websocket != nullptr) {
        *websocket = nullptr;
    }
    if (url == nullptr || urlLength == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    ConnectConfig config = DefaultConnectConfig();
    config.Url = url;
    config.UrlLength = urlLength;
    return ConnectEx(session, &config, websocket);
}

NTSTATUS ConnectEx(khttp::Session* session, const ConnectConfig* config, WebSocket** websocket) noexcept
{
    if (websocket != nullptr) {
        *websocket = nullptr;
    }
    if (config == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    engine::KhWebSocketConnectOptions apiOptions = {};
    FillApiConnectOptions(*config, apiOptions);

    engine::KH_WEBSOCKET apiWs = nullptr;
    NTSTATUS status = engine::KhWebSocketConnectSync(
        khttp::detail::ToApiSession(session),
        &apiOptions,
        &apiWs);
    if (NT_SUCCESS(status) && websocket != nullptr) {
        *websocket = khttp::detail::FromApiWebSocket(apiWs);
    }
    return status;
}

NTSTATUS Connect(khttp::Session* session, const ConnectConfig* config, WebSocket** websocket) noexcept
{
    return ConnectEx(session, config, websocket);
}

NTSTATUS ConnectAsync(khttp::Session* session, const char* url, SIZE_T urlLength, khttp::AsyncOp** operation) noexcept
{
    if (operation != nullptr) {
        *operation = nullptr;
    }
    if (url == nullptr || urlLength == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    ConnectConfig config = DefaultConnectConfig();
    config.Url = url;
    config.UrlLength = urlLength;
    return ConnectAsyncEx(session, &config, operation);
}

NTSTATUS ConnectAsyncEx(khttp::Session* session, const ConnectConfig* config, khttp::AsyncOp** operation) noexcept
{
    if (operation != nullptr) {
        *operation = nullptr;
    }
    if (config == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    engine::KhWebSocketConnectOptions apiOptions = {};
    FillApiConnectOptions(*config, apiOptions);

    engine::KH_ASYNC_OPERATION apiOp = nullptr;
    NTSTATUS status = engine::KhWebSocketConnectAsync(
        khttp::detail::ToApiSession(session),
        &apiOptions,
        &apiOp);
    if (NT_SUCCESS(status) && operation != nullptr) {
        *operation = khttp::detail::FromApiAsyncOp(apiOp);
    }
    return status;
}

NTSTATUS ConnectAsync(khttp::Session* session, const ConnectConfig* config, khttp::AsyncOp** operation) noexcept
{
    return ConnectAsyncEx(session, config, operation);
}

NTSTATUS SendText(WebSocket* websocket, const char* text, SIZE_T textLength) noexcept
{
    return engine::KhWebSocketSendTextSync(khttp::detail::ToApiWebSocket(websocket), text, textLength, nullptr);
}

NTSTATUS SendTextEx(
    WebSocket* websocket,
    const char* text,
    SIZE_T textLength,
    const SendOptions* options) noexcept
{
    engine::KhWebSocketSendOptions apiOptions = {};
    if (options != nullptr) {
        apiOptions.FinalFragment = options->FinalFragment;
    }
    return engine::KhWebSocketSendTextSync(
        khttp::detail::ToApiWebSocket(websocket),
        text,
        textLength,
        options != nullptr ? &apiOptions : nullptr);
}

NTSTATUS SendBinary(WebSocket* websocket, const UCHAR* data, SIZE_T dataLength) noexcept
{
    return engine::KhWebSocketSendBinarySync(khttp::detail::ToApiWebSocket(websocket), data, dataLength, nullptr);
}

NTSTATUS SendBinaryEx(
    WebSocket* websocket,
    const UCHAR* data,
    SIZE_T dataLength,
    const SendOptions* options) noexcept
{
    engine::KhWebSocketSendOptions apiOptions = {};
    if (options != nullptr) {
        apiOptions.FinalFragment = options->FinalFragment;
    }
    return engine::KhWebSocketSendBinarySync(
        khttp::detail::ToApiWebSocket(websocket),
        data,
        dataLength,
        options != nullptr ? &apiOptions : nullptr);
}

NTSTATUS SendContinuation(WebSocket* websocket, const UCHAR* data, SIZE_T dataLength) noexcept
{
    return engine::KhWebSocketSendContinuationSync(khttp::detail::ToApiWebSocket(websocket), data, dataLength, nullptr);
}

NTSTATUS SendContinuationEx(
    WebSocket* websocket,
    const UCHAR* data,
    SIZE_T dataLength,
    const SendOptions* options) noexcept
{
    engine::KhWebSocketSendOptions apiOptions = {};
    if (options != nullptr) {
        apiOptions.FinalFragment = options->FinalFragment;
    }
    return engine::KhWebSocketSendContinuationSync(
        khttp::detail::ToApiWebSocket(websocket),
        data,
        dataLength,
        options != nullptr ? &apiOptions : nullptr);
}

NTSTATUS SendPing(WebSocket* websocket, const UCHAR* payload, SIZE_T payloadLength) noexcept
{
    return engine::KhWebSocketSendPingSync(khttp::detail::ToApiWebSocket(websocket), payload, payloadLength);
}

NTSTATUS SendPong(WebSocket* websocket, const UCHAR* payload, SIZE_T payloadLength) noexcept
{
    return engine::KhWebSocketSendPongSync(khttp::detail::ToApiWebSocket(websocket), payload, payloadLength);
}

NTSTATUS Receive(WebSocket* websocket, Message* message) noexcept
{
    if (message != nullptr) {
        *message = {};
    }
    engine::KhWebSocketMessage apiMessage = {};
    NTSTATUS status = engine::KhWebSocketReceiveSync(
        khttp::detail::ToApiWebSocket(websocket),
        nullptr,
        &apiMessage);
    if (NT_SUCCESS(status) && message != nullptr) {
        message->Type = khttp::detail::FromApiWsMsgType(apiMessage.Type);
        message->Data = apiMessage.Data;
        message->DataLength = apiMessage.DataLength;
        message->Final = apiMessage.FinalFragment;
        message->FinalFragment = apiMessage.FinalFragment;
    }
    return status;
}

NTSTATUS ReceiveEx(
    WebSocket* websocket,
    const ReceiveOptions* options,
    Message* message) noexcept
{
    if (message != nullptr) {
        *message = {};
    }

    engine::KhWebSocketReceiveOptions apiOptions = {};
    if (options != nullptr) {
        apiOptions.MaxMessageBytes = options->MaxMessageBytes;
        apiOptions.AutoAllocate = options->AutoAllocate;
        apiOptions.MessageCallback = reinterpret_cast<engine::KhWebSocketMessageCallback>(options->OnMessage);
        apiOptions.CallbackContext = options->CallbackContext;
    }

    engine::KhWebSocketMessage apiMessage = {};
    NTSTATUS status = engine::KhWebSocketReceiveSync(
        khttp::detail::ToApiWebSocket(websocket),
        options != nullptr ? &apiOptions : nullptr,
        message != nullptr ? &apiMessage : nullptr);
    if (NT_SUCCESS(status) && message != nullptr) {
        message->Type = khttp::detail::FromApiWsMsgType(apiMessage.Type);
        message->Data = apiMessage.Data;
        message->DataLength = apiMessage.DataLength;
        message->Final = apiMessage.FinalFragment;
        message->FinalFragment = apiMessage.FinalFragment;
    }
    return status;
}

NTSTATUS Close(WebSocket* websocket) noexcept
{
    return engine::KhWebSocketCloseSync(khttp::detail::ToApiWebSocket(websocket));
}

NTSTATUS CloseEx(
    WebSocket* websocket,
    USHORT statusCode,
    const UCHAR* reason,
    SIZE_T reasonLength) noexcept
{
    return engine::KhWebSocketCloseExSync(
        khttp::detail::ToApiWebSocket(websocket),
        statusCode,
        reason,
        reasonLength);
}

NTSTATUS SelectedSubprotocol(
    WebSocket* websocket,
    const char** subprotocol,
    SIZE_T* subprotocolLength) noexcept
{
    return engine::KhWebSocketSelectedSubprotocol(
        khttp::detail::ToApiWebSocket(websocket),
        subprotocol,
        subprotocolLength);
}

NTSTATUS AsyncGetWebSocket(khttp::AsyncOp* operation, WebSocket** websocket) noexcept
{
    if (websocket != nullptr) {
        *websocket = nullptr;
    }
    engine::KH_WEBSOCKET apiWs = nullptr;
    NTSTATUS status = engine::KhAsyncGetWebSocket(khttp::detail::ToApiAsyncOp(operation), &apiWs);
    if (NT_SUCCESS(status) && websocket != nullptr) {
        *websocket = khttp::detail::FromApiWebSocket(apiWs);
    }
    return status;
}
}
}
