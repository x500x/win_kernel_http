#include <KernelHttp/http/HttpContentEncoding.h>

#include <KernelHttp/http/HttpCoding.h>

namespace KernelHttp
{
namespace http
{
    namespace
    {
        constexpr SIZE_T MaxContentCodings = 2;

        bool IsOptionalWhitespace(char value) noexcept
        {
            return value == ' ' || value == '\t';
        }

        HttpText TrimOptionalWhitespace(HttpText text) noexcept
        {
            while (text.Length > 0 && IsOptionalWhitespace(text.Data[0])) {
                ++text.Data;
                --text.Length;
            }

            while (text.Length > 0 && IsOptionalWhitespace(text.Data[text.Length - 1])) {
                --text.Length;
            }

            return text;
        }

        _Must_inspect_result_
        NTSTATUS ParseCoding(HttpText token, HttpCoding* coding) noexcept
        {
            if (coding == nullptr) {
                return STATUS_INVALID_PARAMETER;
            }

            token = TrimOptionalWhitespace(token);
            if (token.Length == 0 || token.Data == nullptr) {
                return STATUS_INVALID_NETWORK_RESPONSE;
            }

            if (TextEqualsIgnoreCase(token, MakeText("identity"))) {
                *coding = HttpCoding::Identity;
                return STATUS_SUCCESS;
            }

            if (TextEqualsIgnoreCase(token, MakeText("gzip"))) {
                *coding = HttpCoding::Gzip;
                return STATUS_SUCCESS;
            }

            if (TextEqualsIgnoreCase(token, MakeText("deflate"))) {
                *coding = HttpCoding::Deflate;
                return STATUS_SUCCESS;
            }

            if (TextEqualsIgnoreCase(token, MakeText("br"))) {
                *coding = HttpCoding::Brotli;
                return STATUS_SUCCESS;
            }

            return STATUS_NOT_SUPPORTED;
        }

        _Must_inspect_result_
        NTSTATUS AppendCodings(
            HttpText value,
            HttpCoding* codings,
            SIZE_T* codingCount) noexcept
        {
            if (codings == nullptr || codingCount == nullptr) {
                return STATUS_INVALID_PARAMETER;
            }

            SIZE_T cursor = 0;
            for (;;) {
                const SIZE_T tokenStart = cursor;
                while (cursor < value.Length && value.Data[cursor] != ',') {
                    ++cursor;
                }

                if (*codingCount >= MaxContentCodings) {
                    return STATUS_NOT_SUPPORTED;
                }

                NTSTATUS status = ParseCoding(
                    { value.Data + tokenStart, cursor - tokenStart },
                    &codings[*codingCount]);
                if (!NT_SUCCESS(status)) {
                    return status;
                }

                ++(*codingCount);

                if (cursor == value.Length) {
                    return STATUS_SUCCESS;
                }

                ++cursor;
                if (cursor == value.Length) {
                    return STATUS_INVALID_NETWORK_RESPONSE;
                }
            }
        }
    }

    NTSTATUS HttpContentEncoding::Decode(
        const HttpHeader* headers,
        SIZE_T headerCount,
        const char* body,
        SIZE_T bodyLength,
        const HttpContentDecodeBuffers& buffers,
        HttpContentDecodeResult& result) noexcept
    {
        result = {};
        result.Body = bodyLength == 0 ? nullptr : body;
        result.BodyLength = bodyLength;

        if ((headers == nullptr && headerCount != 0) ||
            (body == nullptr && bodyLength != 0)) {
            result = {};
            return STATUS_INVALID_PARAMETER;
        }

        HeapArray<HttpCoding> codings(MaxContentCodings);
        if (!codings.IsValid()) {
            result = {};
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        SIZE_T codingCount = 0;

        for (SIZE_T index = 0; index < headerCount; ++index) {
            if (!TextEqualsIgnoreCase(headers[index].Name, MakeText("Content-Encoding"))) {
                continue;
            }

            NTSTATUS status = AppendCodings(headers[index].Value, codings.Get(), &codingCount);
            if (!NT_SUCCESS(status)) {
                result = {};
                return status;
            }
        }

        if (codingCount == 0 || bodyLength == 0) {
            return STATUS_SUCCESS;
        }

        HttpCodingDecodeBuffers codingBuffers = {};
        codingBuffers.DecodedBody = buffers.DecodedBody;
        codingBuffers.DecodedBodyCapacity = buffers.DecodedBodyCapacity;
        codingBuffers.ScratchBody = buffers.ScratchBody;
        codingBuffers.ScratchBodyCapacity = buffers.ScratchBodyCapacity;

        HttpCodingDecodeResult decoded = {};
        NTSTATUS status = HttpCodingCodec::DecodeChainReverse(
            codings.Get(),
            codingCount,
            body,
            bodyLength,
            codingBuffers,
            decoded);
        if (!NT_SUCCESS(status)) {
            result = {};
            return status;
        }

        result.Body = decoded.Body;
        result.BodyLength = decoded.BodyLength;
        result.AppliedContentCoding = decoded.AppliedCoding;
        return STATUS_SUCCESS;
    }
}
}
