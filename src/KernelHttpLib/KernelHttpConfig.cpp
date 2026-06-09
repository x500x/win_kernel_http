#include <KernelHttp/KernelHttpConfig.h>

#if !defined(KERNEL_HTTP_USER_MODE_TEST)

_Ret_maybenull_
void* __cdecl operator new(size_t size)
{
    return KernelHttp::AllocateNonPagedPoolBytes(size);
}

_Ret_maybenull_
void* __cdecl operator new[](size_t size)
{
    return KernelHttp::AllocateNonPagedPoolBytes(size);
}

void __cdecl operator delete(void* pointer) noexcept
{
    KernelHttp::FreeNonPagedPoolBytes(pointer);
}

void __cdecl operator delete[](void* pointer) noexcept
{
    KernelHttp::FreeNonPagedPoolBytes(pointer);
}

void __cdecl operator delete(void* pointer, size_t size) noexcept
{
    UNREFERENCED_PARAMETER(size);
    operator delete(pointer);
}

void __cdecl operator delete[](void* pointer, size_t size) noexcept
{
    UNREFERENCED_PARAMETER(size);
    operator delete[](pointer);
}

#endif
