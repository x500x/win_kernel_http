#pragma once

#include <ntddk.h>

#define KERNEL_HTTP_POOL_TAG 'ptHK'
#define KERNEL_HTTP_DRIVER_NAME "KernelHttp"

namespace KernelHttp
{
    constexpr ULONG PoolTag = KERNEL_HTTP_POOL_TAG;
}

_Ret_maybenull_
void* __cdecl operator new(size_t size);

_Ret_maybenull_
void* __cdecl operator new[](size_t size);

void __cdecl operator delete(void* pointer) noexcept;
void __cdecl operator delete[](void* pointer) noexcept;
void __cdecl operator delete(void* pointer, size_t size) noexcept;
void __cdecl operator delete[](void* pointer, size_t size) noexcept;
