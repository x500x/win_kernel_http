#include "KernelHttpConfig.h"
#include "net/WskClient.h"
#include "samples/HttpVerbSamples.h"
#include "samples/Http2VerbSamples.h"

extern "C" NTSYSAPI NTSTATUS NTAPI ZwWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

namespace KernelHttp
{
    namespace
    {
        net::WskClient* g_wskClient = nullptr;

        struct LoadHttpSamplesThreadContext
        {
            NTSTATUS Status;
        };

        void LoadHttpSamplesThread(_In_ PVOID startContext) noexcept;
    }

    NTSTATUS RunHttpSamples(samples::HttpVerbSampleResults* results) noexcept
    {
        if (g_wskClient == nullptr || !g_wskClient->IsInitialized()) {
            return STATUS_DEVICE_NOT_READY;
        }

#if defined(KERNEL_HTTP_LOCAL_HTTPS_SMOKE_ONLY)
        if (results == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        *results = {};
        return samples::RunLocalHttpsSmokeSample(*g_wskClient, &results->LocalHttpsSmoke);
#else
        return samples::RunHttpVerbSamples(*g_wskClient, results);
#endif
    }

    NTSTATUS RunLoadHttpSamples() noexcept
    {
        NTSTATUS finalStatus = STATUS_SUCCESS;
        samples::HttpVerbSampleResults results = {};
        const NTSTATUS status = RunHttpSamples(&results);
        if (!NT_SUCCESS(status)) {
            kprintf("HTTP/HTTPS samples completed with failures: 0x%08X\r\n", static_cast<ULONG>(status));
            finalStatus = status;
        }
        else {
            kprintf("HTTP/HTTPS samples completed successfully\r\n");
        }

#if defined(KERNEL_HTTP_ENABLE_HTTP2_SAMPLE)
        samples::Http2VerbSampleResults http2Results = {};
        const NTSTATUS http2Status = samples::RunHttp2VerbSamples(*g_wskClient, &http2Results);
        if (!NT_SUCCESS(http2Status)) {
            kprintf("HTTP/2 samples completed with failures: 0x%08X\r\n", static_cast<ULONG>(http2Status));
            if (NT_SUCCESS(finalStatus)) {
                finalStatus = http2Status;
            }
        }
        else {
            kprintf("HTTP/2 samples completed successfully\r\n");
        }
#endif

        return finalStatus;
    }

    void LoadHttpSamplesThread(_In_ PVOID startContext) noexcept
    {
        LoadHttpSamplesThreadContext* context = static_cast<LoadHttpSamplesThreadContext*>(startContext);
        if (context == nullptr) {
            PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
            return;
        }

        context->Status = RunLoadHttpSamples();
        PsTerminateSystemThread(context->Status);
    }

    _Use_decl_annotations_
    extern "C" void DriverUnload(PDRIVER_OBJECT driverObject)
    {
        UNREFERENCED_PARAMETER(driverObject);

        if (g_wskClient != nullptr) {
            kprintf("DriverUnload begin\r\n");
            g_wskClient->Shutdown();
            delete g_wskClient;
            g_wskClient = nullptr;
            kprintf("DriverUnload complete\r\n");
        }
    }
}

_Use_decl_annotations_
extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath)
{
    UNREFERENCED_PARAMETER(registryPath);

    kprintf("DriverEntry begin\r\n");
    driverObject->DriverUnload = KernelHttp::DriverUnload;

    KernelHttp::g_wskClient = new KernelHttp::net::WskClient();
    if (KernelHttp::g_wskClient == nullptr) {
        kprintf("WskClient allocation failed\r\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NTSTATUS status = KernelHttp::g_wskClient->Initialize();
    if (!NT_SUCCESS(status)) {
        kprintf("WSK initialize failed: 0x%08X\r\n", static_cast<ULONG>(status));
        delete KernelHttp::g_wskClient;
        KernelHttp::g_wskClient = nullptr;
        return status;
    }

    kprintf("WSK initialized, running load-time HTTP/HTTPS requests\r\n");

    KernelHttp::LoadHttpSamplesThreadContext sampleThreadContext = { STATUS_UNSUCCESSFUL };
    OBJECT_ATTRIBUTES objectAttributes = {};
    InitializeObjectAttributes(&objectAttributes, nullptr, OBJ_KERNEL_HANDLE, nullptr, nullptr);

    HANDLE sampleThreadHandle = nullptr;
    status = PsCreateSystemThread(
        &sampleThreadHandle,
        THREAD_ALL_ACCESS,
        &objectAttributes,
        nullptr,
        nullptr,
        KernelHttp::LoadHttpSamplesThread,
        &sampleThreadContext);
    if (!NT_SUCCESS(status)) {
        kprintf("Failed to create load-time sample thread: 0x%08X\r\n", static_cast<ULONG>(status));
        KernelHttp::g_wskClient->Shutdown();
        delete KernelHttp::g_wskClient;
        KernelHttp::g_wskClient = nullptr;
        return status;
    }

    status = ZwWaitForSingleObject(sampleThreadHandle, FALSE, nullptr);
    ZwClose(sampleThreadHandle);
    if (!NT_SUCCESS(status)) {
        kprintf("Failed to wait for load-time sample thread: 0x%08X\r\n", static_cast<ULONG>(status));
        KernelHttp::g_wskClient->Shutdown();
        delete KernelHttp::g_wskClient;
        KernelHttp::g_wskClient = nullptr;
        return status;
    }

    status = sampleThreadContext.Status;
    if (!NT_SUCCESS(status)) {
        KernelHttp::g_wskClient->Shutdown();
        delete KernelHttp::g_wskClient;
        KernelHttp::g_wskClient = nullptr;
    }

    kprintf("DriverEntry complete: 0x%08X\r\n", static_cast<ULONG>(status));

    return status;
}

_Ret_maybenull_
void* __cdecl operator new(size_t size)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KernelHttp::PoolTag);
}

_Ret_maybenull_
void* __cdecl operator new[](size_t size)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, KernelHttp::PoolTag);
}

void __cdecl operator delete(void* pointer) noexcept
{
    if (pointer != nullptr) {
        ExFreePoolWithTag(pointer, KernelHttp::PoolTag);
    }
}

void __cdecl operator delete[](void* pointer) noexcept
{
    if (pointer != nullptr) {
        ExFreePoolWithTag(pointer, KernelHttp::PoolTag);
    }
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
