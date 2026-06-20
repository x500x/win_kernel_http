#include <KernelHttp/khttp/Options.h>

namespace khttp
{
NTSTATUS SendOptionsCreate(SendOptions** options) noexcept
{
    if (options != nullptr) {
        *options = nullptr;
    }
    if (options == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    auto* created = ::KernelHttp::AllocateNonPagedObject<SendOptions>();
    if (created == nullptr) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    *options = created;
    return STATUS_SUCCESS;
}

void SendOptionsRelease(SendOptions* options) noexcept
{
    ::KernelHttp::FreeNonPagedObject(options);
}

NTSTATUS AsyncOptionsCreate(AsyncOptions** options) noexcept
{
    if (options != nullptr) {
        *options = nullptr;
    }
    if (options == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }

    auto* created = ::KernelHttp::AllocateNonPagedObject<AsyncOptions>();
    if (created == nullptr) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    *options = created;
    return STATUS_SUCCESS;
}

void AsyncOptionsRelease(AsyncOptions* options) noexcept
{
    ::KernelHttp::FreeNonPagedObject(options);
}
}
