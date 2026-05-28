#include "Test.h"
#include "Detail.h"
#include "../engine/Engine.h"
#include "../http/HttpTypes.h"

namespace KernelHttp
{
namespace khttp
{
#if defined(KERNEL_HTTP_USER_MODE_TEST)
namespace test
{
    void SetHttpTransport(HttpTransportCallback callback, void* context) noexcept
    {
        engine::KhTestSetHttpTransport(callback, context);
    }

    void SetWebSocketTransport(
        WebSocketConnectCallback connectCallback,
        WebSocketSendCallback sendCallback,
        WebSocketReceiveCallback receiveCallback,
        WebSocketCloseCallback closeCallback,
        void* context) noexcept
    {
        engine::KhTestSetWebSocketTransport(
            connectCallback,
            sendCallback,
            receiveCallback,
            closeCallback,
            context);
    }

    void SetCurrentIrql(ULONG irql) noexcept
    {
        engine::KhTestSetCurrentIrql(irql);
    }

    void ResetCurrentIrql() noexcept
    {
        engine::KhTestResetCurrentIrql();
    }

    void SetAsyncAutoRun(bool enabled) noexcept
    {
        engine::KhTestSetAsyncAutoRun(enabled);
    }

    NTSTATUS RunAsyncOperation(AsyncOp* operation) noexcept
    {
        return engine::KhTestRunAsyncOperation(detail::ToApiAsyncOp(operation));
    }
}
#endif

}
}
