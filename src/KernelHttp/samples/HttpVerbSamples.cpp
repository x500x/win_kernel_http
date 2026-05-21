#include "samples/HttpVerbSamples.h"

#include "client/HttpClient.h"

namespace KernelHttp
{
namespace samples
{
    namespace
    {
        constexpr SIZE_T SampleRequestBufferLength = 1024;
        constexpr SIZE_T SampleResponseBufferLength = 16384;
        constexpr SIZE_T SampleDecodedBodyBufferLength = 8192;
        constexpr SIZE_T SampleScratchBodyBufferLength = 8192;
        constexpr SIZE_T SampleHeaderCapacity = 32;
        constexpr SIZE_T SampleLogChunkLength = 384;
        constexpr const wchar_t* HttpBinServerName = L"httpbin.org";
        constexpr const wchar_t* HttpBinServiceName = L"80";

        struct SampleIoBuffers final
        {
            char Request[SampleRequestBufferLength] = {};
            char Response[SampleResponseBufferLength] = {};
            char DecodedBody[SampleDecodedBodyBufferLength] = {};
            char ScratchBody[SampleScratchBodyBufferLength] = {};
            http::HttpHeader Headers[SampleHeaderCapacity] = {};
        };

        void LogHttpText(_In_opt_ const char* label, http::HttpText value) noexcept
        {
            if (label != nullptr) {
                kprintf("%s%.*s\r\n", label, static_cast<int>(value.Length), value.Data != nullptr ? value.Data : "");
            }
            else {
                kprintf("%.*s\r\n", static_cast<int>(value.Length), value.Data != nullptr ? value.Data : "");
            }
        }

        void LogBody(const char* body, SIZE_T bodyLength) noexcept
        {
            if (body == nullptr || bodyLength == 0) {
                kprintf("[body]\r\n<empty>\r\n");
                return;
            }

            kprintf("[body]\r\n");
            for (SIZE_T offset = 0; offset < bodyLength; offset += SampleLogChunkLength) {
                const SIZE_T remaining = bodyLength - offset;
                const SIZE_T chunkLength = remaining < SampleLogChunkLength ? remaining : SampleLogChunkLength;
                kprintf("%.*s\r\n", static_cast<int>(chunkLength), body + offset);
            }
        }

        void LogResponse(const char* methodName, const http::HttpResponse& response) noexcept
        {
            kprintf("[%s] status=%u version=%u.%u bodyKind=%u bodyLength=%Iu consumed=%Iu\r\n",
                methodName,
                response.StatusCode,
                response.MajorVersion,
                response.MinorVersion,
                static_cast<unsigned>(response.BodyKind),
                response.BodyLength,
                response.BytesConsumed);

            LogHttpText("[status-line] reason=", response.ReasonPhrase);

            for (SIZE_T index = 0; index < response.HeaderCount; ++index) {
                const http::HttpHeader& header = response.Headers[index];
                kprintf("[header] %.*s: %.*s\r\n",
                    static_cast<int>(header.Name.Length),
                    header.Name.Data != nullptr ? header.Name.Data : "",
                    static_cast<int>(header.Value.Length),
                    header.Value.Data != nullptr ? header.Value.Data : "");
            }

            LogBody(response.Body, response.BodyLength);
        }

        void LogRequestStart(
            const char* sampleName,
            http::HttpText path,
            http::HttpText acceptEncoding) noexcept
        {
            kprintf("[%s] request path=%.*s accept-encoding=%.*s\r\n",
                sampleName,
                static_cast<int>(path.Length),
                path.Data != nullptr ? path.Data : "",
                static_cast<int>(acceptEncoding.Length),
                acceptEncoding.Data != nullptr ? acceptEncoding.Data : "");
        }

        _Must_inspect_result_
        NTSTATUS SendSampleRequest(
            _Inout_ net::WskClient& wskClient,
            _In_z_ const wchar_t* serverName,
            _In_ const wchar_t* serviceName,
            _In_ const char* methodName,
            _In_ const http::HttpRequestBuildOptions& request,
            bool responseBodyForbidden,
            _Out_ HttpVerbSampleResult& result) noexcept
        {
            result = {};

            auto* buffers = new SampleIoBuffers();
            if (buffers == nullptr) {
                result.Status = STATUS_INSUFFICIENT_RESOURCES;
                return result.Status;
            }

            client::HttpResponseBuffers responseBuffers = {};
            responseBuffers.RequestBuffer = buffers->Request;
            responseBuffers.RequestBufferLength = sizeof(buffers->Request);
            responseBuffers.ResponseBuffer = buffers->Response;
            responseBuffers.ResponseBufferLength = sizeof(buffers->Response);
            responseBuffers.DecodedBodyBuffer = buffers->DecodedBody;
            responseBuffers.DecodedBodyBufferLength = sizeof(buffers->DecodedBody);
            responseBuffers.ScratchBodyBuffer = buffers->ScratchBody;
            responseBuffers.ScratchBodyBufferLength = sizeof(buffers->ScratchBody);
            responseBuffers.Headers = buffers->Headers;
            responseBuffers.HeaderCapacity = SampleHeaderCapacity;

            client::HttpRequestOptions options = {};
            options.ServerName = serverName;
            options.ServiceName = serviceName;
            options.Request = request;
            options.ResponseBodyForbidden = responseBodyForbidden;

            http::HttpResponse response = {};
            client::HttpClient client;
            http::HttpText acceptEncoding = {};
            const http::HttpHeader* acceptEncodingHeader = nullptr;
            for (SIZE_T index = 0; index < request.ExtraHeaderCount; ++index) {
                if (http::TextEqualsIgnoreCase(request.ExtraHeaders[index].Name, http::MakeText("Accept-Encoding"))) {
                    acceptEncodingHeader = &request.ExtraHeaders[index];
                    break;
                }
            }
            if (acceptEncodingHeader != nullptr) {
                acceptEncoding = acceptEncodingHeader->Value;
            }

            LogRequestStart(methodName, request.Path, acceptEncoding);
            result.Status = client.SendRequest(wskClient, options, responseBuffers, response);
            if (NT_SUCCESS(result.Status)) {
                result.StatusCode = response.StatusCode;
                result.HeaderCount = response.HeaderCount;
                result.BodyLength = response.BodyLength;
                LogResponse(methodName, response);
            }
            else {
                kprintf("[%s] request failed: 0x%08X\r\n", methodName, static_cast<ULONG>(result.Status));
            }

            delete buffers;
            return result.Status;
        }

        NTSTATUS MergeSampleStatus(NTSTATUS current, NTSTATUS next) noexcept
        {
            return NT_SUCCESS(current) ? next : current;
        }

        _Must_inspect_result_
        NTSTATUS SendHttpBinGet(
            _Inout_ net::WskClient& wskClient,
            _In_z_ const char* sampleName,
            _In_ http::HttpText path,
            _In_ http::HttpText acceptEncoding,
            _Out_ HttpVerbSampleResult& result) noexcept
        {
            const http::HttpHeader headers[] = {
                { http::MakeText("Accept"), http::MakeText("*/*") },
                { http::MakeText("Accept-Encoding"), acceptEncoding }
            };

            http::HttpRequestBuildOptions request = {};
            request.Method = http::HttpMethod::Get;
            request.Path = path;
            request.Host = http::MakeText("httpbin.org");
            request.UserAgent = http::MakeText("KernelHttp/0.1");
            request.Connection = http::HttpConnectionDirective::Close;
            request.ExtraHeaders = headers;
            request.ExtraHeaderCount = sizeof(headers) / sizeof(headers[0]);

            return SendSampleRequest(
                wskClient,
                HttpBinServerName,
                HttpBinServiceName,
                sampleName,
                request,
                false,
                result);
        }
    }

    NTSTATUS RunHttpVerbSamples(
        net::WskClient& wskClient,
        HttpVerbSampleResults* results) noexcept
    {
        if (results == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        *results = {};

        const http::HttpHeader commonHeaders[] = {
            { http::MakeText("Accept"), http::MakeText("*/*") },
            { http::MakeText("Accept-Encoding"), http::MakeText("gzip, deflate, br, identity") }
        };

        NTSTATUS status = SendHttpBinGet(
            wskClient,
            "ENCODING identity",
            http::MakeText("/get"),
            http::MakeText("identity"),
            results->IdentityEncoding);

        status = MergeSampleStatus(
            status,
            SendHttpBinGet(
                wskClient,
                "ENCODING gzip",
                http::MakeText("/gzip"),
                http::MakeText("gzip"),
                results->GzipEncoding));

        status = MergeSampleStatus(
            status,
            SendHttpBinGet(
                wskClient,
                "ENCODING deflate",
                http::MakeText("/deflate"),
                http::MakeText("deflate"),
                results->DeflateEncoding));

        status = MergeSampleStatus(
            status,
            SendHttpBinGet(
                wskClient,
                "ENCODING br",
                http::MakeText("/brotli"),
                http::MakeText("br"),
                results->BrotliEncoding));

        http::HttpRequestBuildOptions getHttpBin = {};
        getHttpBin.Method = http::HttpMethod::Get;
        getHttpBin.Path = http::MakeText("/get");
        getHttpBin.Host = http::MakeText("httpbin.org");
        getHttpBin.UserAgent = http::MakeText("KernelHttp/0.1");
        getHttpBin.Connection = http::HttpConnectionDirective::Close;
        getHttpBin.ExtraHeaders = commonHeaders;
        getHttpBin.ExtraHeaderCount = sizeof(commonHeaders) / sizeof(commonHeaders[0]);

        status = MergeSampleStatus(
            status,
            SendSampleRequest(
                wskClient,
                HttpBinServerName,
                HttpBinServiceName,
                "GET",
                getHttpBin,
                false,
                results->GetHttpBin));

        const char postBody[] = "{\"source\":\"kernel-http\",\"method\":\"POST\"}";
        http::HttpRequestBuildOptions postHttpBin = {};
        postHttpBin.Method = http::HttpMethod::Post;
        postHttpBin.Path = http::MakeText("/post");
        postHttpBin.Host = http::MakeText("httpbin.org");
        postHttpBin.UserAgent = http::MakeText("KernelHttp/0.1");
        postHttpBin.ContentType = http::MakeText("application/json");
        postHttpBin.Connection = http::HttpConnectionDirective::Close;
        postHttpBin.Body = postBody;
        postHttpBin.BodyLength = sizeof(postBody) - 1;
        postHttpBin.ExtraHeaders = commonHeaders;
        postHttpBin.ExtraHeaderCount = sizeof(commonHeaders) / sizeof(commonHeaders[0]);

        status = MergeSampleStatus(
            status,
            SendSampleRequest(
                wskClient,
                HttpBinServerName,
                HttpBinServiceName,
                "POST",
                postHttpBin,
                false,
                results->PostHttpBin));

        const char putBody[] = "{\"source\":\"kernel-http\",\"method\":\"PUT\"}";
        http::HttpRequestBuildOptions putHttpBin = {};
        putHttpBin.Method = http::HttpMethod::Put;
        putHttpBin.Path = http::MakeText("/put");
        putHttpBin.Host = http::MakeText("httpbin.org");
        putHttpBin.UserAgent = http::MakeText("KernelHttp/0.1");
        putHttpBin.ContentType = http::MakeText("application/json");
        putHttpBin.Connection = http::HttpConnectionDirective::Close;
        putHttpBin.Body = putBody;
        putHttpBin.BodyLength = sizeof(putBody) - 1;
        putHttpBin.ExtraHeaders = commonHeaders;
        putHttpBin.ExtraHeaderCount = sizeof(commonHeaders) / sizeof(commonHeaders[0]);

        status = MergeSampleStatus(
            status,
            SendSampleRequest(
                wskClient,
                HttpBinServerName,
                HttpBinServiceName,
                "PUT",
                putHttpBin,
                false,
                results->PutHttpBin));

        const char patchBody[] = "{\"source\":\"kernel-http\",\"method\":\"PATCH\"}";
        http::HttpRequestBuildOptions patchHttpBin = {};
        patchHttpBin.Method = http::HttpMethod::Patch;
        patchHttpBin.Path = http::MakeText("/patch");
        patchHttpBin.Host = http::MakeText("httpbin.org");
        patchHttpBin.UserAgent = http::MakeText("KernelHttp/0.1");
        patchHttpBin.ContentType = http::MakeText("application/json");
        patchHttpBin.Connection = http::HttpConnectionDirective::Close;
        patchHttpBin.Body = patchBody;
        patchHttpBin.BodyLength = sizeof(patchBody) - 1;
        patchHttpBin.ExtraHeaders = commonHeaders;
        patchHttpBin.ExtraHeaderCount = sizeof(commonHeaders) / sizeof(commonHeaders[0]);

        status = MergeSampleStatus(
            status,
            SendSampleRequest(
                wskClient,
                HttpBinServerName,
                HttpBinServiceName,
                "PATCH",
                patchHttpBin,
                false,
                results->PatchHttpBin));

        const char deleteBody[] = "{\"source\":\"kernel-http\",\"method\":\"DELETE\"}";
        http::HttpRequestBuildOptions deleteHttpBin = {};
        deleteHttpBin.Method = http::HttpMethod::DeleteMethod;
        deleteHttpBin.Path = http::MakeText("/delete");
        deleteHttpBin.Host = http::MakeText("httpbin.org");
        deleteHttpBin.UserAgent = http::MakeText("KernelHttp/0.1");
        deleteHttpBin.ContentType = http::MakeText("application/json");
        deleteHttpBin.Connection = http::HttpConnectionDirective::Close;
        deleteHttpBin.Body = deleteBody;
        deleteHttpBin.BodyLength = sizeof(deleteBody) - 1;
        deleteHttpBin.ExtraHeaders = commonHeaders;
        deleteHttpBin.ExtraHeaderCount = sizeof(commonHeaders) / sizeof(commonHeaders[0]);

        status = MergeSampleStatus(
            status,
            SendSampleRequest(
                wskClient,
                HttpBinServerName,
                HttpBinServiceName,
                "DELETE",
                deleteHttpBin,
                false,
                results->DeleteHttpBin));

        http::HttpRequestBuildOptions headHttpBin = {};
        headHttpBin.Method = http::HttpMethod::Head;
        headHttpBin.Path = http::MakeText("/get");
        headHttpBin.Host = http::MakeText("httpbin.org");
        headHttpBin.UserAgent = http::MakeText("KernelHttp/0.1");
        headHttpBin.Connection = http::HttpConnectionDirective::Close;
        headHttpBin.ExtraHeaders = commonHeaders;
        headHttpBin.ExtraHeaderCount = sizeof(commonHeaders) / sizeof(commonHeaders[0]);

        status = MergeSampleStatus(
            status,
            SendSampleRequest(
                wskClient,
                HttpBinServerName,
                HttpBinServiceName,
                "HEAD",
                headHttpBin,
                true,
                results->HeadHttpBin));

        http::HttpRequestBuildOptions optionsHttpBin = {};
        optionsHttpBin.Method = http::HttpMethod::Options;
        optionsHttpBin.Path = http::MakeText("/");
        optionsHttpBin.Host = http::MakeText("httpbin.org");
        optionsHttpBin.UserAgent = http::MakeText("KernelHttp/0.1");
        optionsHttpBin.Connection = http::HttpConnectionDirective::Close;
        optionsHttpBin.ExtraHeaders = commonHeaders;
        optionsHttpBin.ExtraHeaderCount = sizeof(commonHeaders) / sizeof(commonHeaders[0]);

        status = MergeSampleStatus(
            status,
            SendSampleRequest(
                wskClient,
                HttpBinServerName,
                HttpBinServiceName,
                "OPTIONS",
                optionsHttpBin,
                false,
                results->OptionsHttpBin));

        return status;
    }
}
}
