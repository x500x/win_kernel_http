#pragma once

#if defined(KERNEL_HTTP_USER_MODE_TEST)
#include <KernelHttp/http/HttpTypes.h>
#include <stdarg.h>
#include <stdio.h>

namespace KernelHttp
{
namespace testlog
{
    inline NTSTATUS Initialize(_In_z_ const char* path) noexcept
    {
        UNREFERENCED_PARAMETER(path);
        return STATUS_SUCCESS;
    }

    inline void Shutdown() noexcept
    {
    }

    inline void Print(_In_z_ const char* format, ...) noexcept
    {
        va_list args = {};
        va_start(args, format);
        (void)vprintf(format, args);
        va_end(args);
    }

    inline void WriteRaw(_In_reads_bytes_opt_(length) const char* data, SIZE_T length) noexcept
    {
        if (data == nullptr || length == 0) {
            return;
        }
        (void)fwrite(data, 1, length, stdout);
    }
}
}
#else
#include <KernelHttp/KernelHttpConfig.h>

namespace KernelHttp
{
namespace testlog
{
    _Must_inspect_result_
    NTSTATUS Initialize(_In_z_ const char* path) noexcept;

    void Shutdown() noexcept;

    void Print(_In_z_ _Printf_format_string_ const char* format, ...) noexcept;

    void WriteRaw(_In_reads_bytes_opt_(length) const char* data, SIZE_T length) noexcept;
}
}
#endif

#undef kprintf
#define kprintf(...) KernelHttp::testlog::Print(__VA_ARGS__)

#undef KHTTP_SAMPLE_LOG
#define KHTTP_SAMPLE_LOG(...) kprintf(__VA_ARGS__)
