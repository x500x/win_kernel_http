#pragma once

#if defined(KERNEL_HTTP_USER_MODE_TEST)
#include <KernelHttp/http/HttpTypes.h>
#else
#include <ntddk.h>
#endif

namespace KernelHttp
{
    constexpr SIZE_T KH_HARD_MAX_RESPONSE_BYTES = 64 * 1024 * 1024;
    constexpr SIZE_T KH_HARD_MAX_HEADER_SECTION = 64 * 1024;
    constexpr SIZE_T KH_HARD_MAX_HEADERS = 200;
    constexpr SIZE_T KH_HARD_MAX_DECODED_BYTES = 16 * 1024 * 1024;
    constexpr ULONG KH_HARD_MAX_H2_CONCURRENT_STREAMS_LOCAL = 100;
    constexpr ULONGLONG KH_HARD_MAX_CONNECTION_BYTES = 512ULL * 1024 * 1024;
    constexpr ULONG KH_HARD_MAX_CONNECTION_FRAMES = 1U << 20;
    constexpr ULONG KH_HARD_MAX_CONNECTION_CONTROL_SIGNALS = 4096;
}
