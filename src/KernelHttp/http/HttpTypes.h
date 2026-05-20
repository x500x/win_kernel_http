#pragma once

#if defined(KERNEL_HTTP_USER_MODE_TEST)
#include <stddef.h>
#include <stdint.h>
#include <string.h>

using NTSTATUS = long;
using SIZE_T = size_t;
using ULONG = uint32_t;
using USHORT = uint16_t;
using UCHAR = uint8_t;

#ifndef RtlCopyMemory
#define RtlCopyMemory(Destination, Source, Length) memcpy((Destination), (Source), (Length))
#endif

#ifndef RtlZeroMemory
#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#endif

#ifndef RtlSecureZeroMemory
#define RtlSecureZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#endif

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_PENDING
#define STATUS_PENDING ((NTSTATUS)0x00000103L)
#endif

#ifndef STATUS_MORE_PROCESSING_REQUIRED
#define STATUS_MORE_PROCESSING_REQUIRED ((NTSTATUS)0xC0000016L)
#endif

#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif

#ifndef STATUS_INTEGER_OVERFLOW
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095L)
#endif

#ifndef STATUS_INVALID_NETWORK_RESPONSE
#define STATUS_INVALID_NETWORK_RESPONSE ((NTSTATUS)0xC00000C3L)
#endif

#ifndef STATUS_NOT_SUPPORTED
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xC00000BBL)
#endif

#ifndef _Must_inspect_result_
#define _Must_inspect_result_
#endif

#ifndef _In_reads_bytes_
#define _In_reads_bytes_(x)
#endif

#ifndef _In_reads_bytes_opt_
#define _In_reads_bytes_opt_(x)
#endif

#ifndef _In_reads_
#define _In_reads_(x)
#endif

#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(x)
#endif

#ifndef _Out_
#define _Out_
#endif

#ifndef _Outptr_result_bytebuffer_
#define _Outptr_result_bytebuffer_(x)
#endif

#ifndef _Out_opt_
#define _Out_opt_
#endif

#ifndef _Inout_
#define _Inout_
#endif

#ifndef _In_
#define _In_
#endif

#ifndef _In_opt_
#define _In_opt_
#endif

#ifndef _Ret_maybenull_
#define _Ret_maybenull_
#endif

#ifndef _In_z_
#define _In_z_
#endif

#else
#include "KernelHttpConfig.h"
#endif

namespace KernelHttp
{
namespace http
{
    struct HttpText final
    {
        const char* Data = nullptr;
        SIZE_T Length = 0;
    };

    struct HttpHeader final
    {
        HttpText Name = {};
        HttpText Value = {};
    };

    _Must_inspect_result_
    HttpText MakeText(_In_opt_ const char* value) noexcept;

    _Must_inspect_result_
    bool TextEqualsIgnoreCase(HttpText left, HttpText right) noexcept;

    _Must_inspect_result_
    bool HeaderValueHasToken(HttpText value, HttpText token) noexcept;
}
}
