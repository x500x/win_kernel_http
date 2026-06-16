#include <KernelHttp/khttp/Response.h>
#include <KernelHttp/khttp/Detail.h>
#include <KernelHttp/engine/Engine.h>

namespace khttp
{
ULONG ResponseStatusCode(const Response* response) noexcept
{
    if (response == nullptr) {
        return 0;
    }
    ::KernelHttp::engine::KhResponseView view = {};
    NTSTATUS status = ::KernelHttp::engine::KhResponseGetView(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)),
        &view);
    return NT_SUCCESS(status) ? view.StatusCode : 0;
}

const UCHAR* ResponseBody(const Response* response) noexcept
{
    if (response == nullptr) {
        return nullptr;
    }
    ::KernelHttp::engine::KhResponseView view = {};
    NTSTATUS status = ::KernelHttp::engine::KhResponseGetView(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)),
        &view);
    return NT_SUCCESS(status) ? view.Body : nullptr;
}

SIZE_T ResponseBodyLength(const Response* response) noexcept
{
    if (response == nullptr) {
        return 0;
    }
    ::KernelHttp::engine::KhResponseView view = {};
    NTSTATUS status = ::KernelHttp::engine::KhResponseGetView(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)),
        &view);
    return NT_SUCCESS(status) ? view.BodyLength : 0;
}

SIZE_T ResponseHeaderCount(const Response* response) noexcept
{
    return ::KernelHttp::engine::KhResponseHeaderCount(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)));
}

SIZE_T ResponseTrailerCount(const Response* response) noexcept
{
    return ::KernelHttp::engine::KhResponseTrailerCount(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)));
}

NTSTATUS ResponseGetHeader(
    const Response* response,
    const char* name,
    SIZE_T nameLength,
    const char** value,
    SIZE_T* valueLength) noexcept
{
    return ::KernelHttp::engine::KhResponseGetHeader(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)),
        name,
        nameLength,
        value,
        valueLength);
}

NTSTATUS ResponseGetHeaderAt(
    const Response* response,
    SIZE_T index,
    const char** name,
    SIZE_T* nameLength,
    const char** value,
    SIZE_T* valueLength) noexcept
{
    return ::KernelHttp::engine::KhResponseGetHeaderAt(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)),
        index,
        name,
        nameLength,
        value,
        valueLength);
}

NTSTATUS ResponseGetTrailer(
    const Response* response,
    const char* name,
    SIZE_T nameLength,
    const char** value,
    SIZE_T* valueLength) noexcept
{
    return ::KernelHttp::engine::KhResponseGetTrailer(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)),
        name,
        nameLength,
        value,
        valueLength);
}

NTSTATUS ResponseGetTrailerAt(
    const Response* response,
    SIZE_T index,
    const char** name,
    SIZE_T* nameLength,
    const char** value,
    SIZE_T* valueLength) noexcept
{
    return ::KernelHttp::engine::KhResponseGetTrailerAt(
        const_cast<::KernelHttp::engine::KH_RESPONSE>(detail::ToApiResponseConst(response)),
        index,
        name,
        nameLength,
        value,
        valueLength);
}

void ResponseRelease(Response* response) noexcept
{
    ::KernelHttp::engine::KhResponseRelease(detail::ToApiResponse(response));
}
}
