#pragma once

#include <KernelHttp/http/HttpRequest.h>
#include <KernelHttp/http/HttpResponse.h>

namespace KernelHttp
{
namespace client
{
    struct ProxyConnectRequestOptions final
    {
        http::HttpText Authority = {};
        http::HttpText UserAgent = {};
        const http::HttpHeader* Headers = nullptr;
        SIZE_T HeaderCount = 0;
    };

    _Must_inspect_result_
    NTSTATUS BuildProxyConnectRequest(
        _In_ const ProxyConnectRequestOptions& options,
        _Out_writes_bytes_(requestCapacity) char* requestBuffer,
        SIZE_T requestCapacity,
        _Out_ SIZE_T* requestLength) noexcept;

    _Must_inspect_result_
    bool IsSuccessfulProxyConnectResponse(_In_ const http::HttpResponse& response) noexcept;
}
}
