#pragma once

#include "../http/HttpTypes.h"

#if !defined(KERNEL_HTTP_USER_MODE_TEST)
#include <ntddk.h>
#endif

namespace KernelHttp
{
namespace khttp
{
    struct Session;
}

namespace samples
{
    struct HighLevelApiSampleResult final
    {
        NTSTATUS Status = STATUS_SUCCESS;
        ULONG StatusCode = 0;
        SIZE_T BodyLength = 0;
    };

    struct HighLevelApiSampleResults final
    {
        HighLevelApiSampleResult HttpGet = {};
        HighLevelApiSampleResult HttpGetAsync = {};
        HighLevelApiSampleResult HttpPost = {};
        HighLevelApiSampleResult HttpPostAsync = {};
        HighLevelApiSampleResult HttpPut = {};
        HighLevelApiSampleResult HttpPatch = {};
        HighLevelApiSampleResult HttpDelete = {};
        HighLevelApiSampleResult HttpHead = {};
        HighLevelApiSampleResult HttpOptions = {};
        HighLevelApiSampleResult HttpsVerifyGet = {};
        HighLevelApiSampleResult HttpsNoVerifyGet = {};
        HighLevelApiSampleResult HttpsRequestBuilder = {};
        HighLevelApiSampleResult WebSocketEcho = {};
    };

    _Must_inspect_result_
    NTSTATUS RunHighLevelApiSamples(
        _In_ khttp::Session* session,
        _Out_ HighLevelApiSampleResults* results) noexcept;
}
}
