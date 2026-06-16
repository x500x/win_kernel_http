#include <KernelHttp/khttp/AsyncOp.h>
#include <KernelHttp/khttp/Detail.h>
#include <KernelHttp/engine/Async.h>
#include <KernelHttp/engine/Engine.h>

namespace khttp
{
NTSTATUS AsyncWait(AsyncOp* operation, ULONG timeoutMs) noexcept
{
    return ::KernelHttp::engine::KhAsyncWait(detail::ToApiAsyncOp(operation), timeoutMs);
}

NTSTATUS AsyncCancel(AsyncOp* operation) noexcept
{
    return ::KernelHttp::engine::KhAsyncCancel(detail::ToApiAsyncOp(operation));
}

NTSTATUS AsyncGetStatus(const AsyncOp* operation) noexcept
{
#if defined(KERNEL_HTTP_USER_MODE_TEST)
    return ::KernelHttp::engine::KhTestAsyncStatus(const_cast<::KernelHttp::engine::KH_ASYNC_OPERATION>(
        reinterpret_cast<const ::KernelHttp::engine::KhAsyncOperation*>(operation)));
#else
    if (operation == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    return reinterpret_cast<const ::KernelHttp::engine::KhAsyncOperation*>(operation)->Status;
#endif
}

bool AsyncIsCompleted(const AsyncOp* operation) noexcept
{
#if defined(KERNEL_HTTP_USER_MODE_TEST)
    return ::KernelHttp::engine::KhTestAsyncIsCompleted(const_cast<::KernelHttp::engine::KH_ASYNC_OPERATION>(
        reinterpret_cast<const ::KernelHttp::engine::KhAsyncOperation*>(operation)));
#else
    if (operation == nullptr) {
        return false;
    }
    return ::KernelHttp::engine::KhAsyncOperationState(detail::ToApiAsyncOp(const_cast<AsyncOp*>(operation))) ==
        ::KernelHttp::engine::KhAsyncState::Completed;
#endif
}

bool AsyncIsCanceled(const AsyncOp* operation) noexcept
{
#if defined(KERNEL_HTTP_USER_MODE_TEST)
    return ::KernelHttp::engine::KhTestAsyncIsCanceled(const_cast<::KernelHttp::engine::KH_ASYNC_OPERATION>(
        reinterpret_cast<const ::KernelHttp::engine::KhAsyncOperation*>(operation)));
#else
    if (operation == nullptr) {
        return false;
    }
    return reinterpret_cast<const ::KernelHttp::engine::KhAsyncOperation*>(operation)->Canceled != 0;
#endif
}

NTSTATUS AsyncGetResponse(AsyncOp* operation, Response** response) noexcept
{
    if (response != nullptr) {
        *response = nullptr;
    }
    ::KernelHttp::engine::KH_RESPONSE apiResp = nullptr;
    NTSTATUS status = ::KernelHttp::engine::KhAsyncGetHttpResponse(detail::ToApiAsyncOp(operation), &apiResp);
    if (NT_SUCCESS(status) && response != nullptr) {
        *response = detail::FromApiResponse(apiResp);
    }
    return status;
}

void AsyncRelease(AsyncOp* operation) noexcept
{
    ::KernelHttp::engine::KhAsyncRelease(detail::ToApiAsyncOp(operation));
}
}
