#include <KernelHttp/net/WskClient.h>
#include "WskSync.h"

namespace KernelHttp
{
namespace net
{
    namespace
    {
        const WSK_CLIENT_DISPATCH WskClientDispatch = {
            MAKE_WSK_VERSION(1, 0),
            0,
            nullptr
        };

        struct ResolveRequestContext final
        {
            wchar_t* NodeName = nullptr;
            wchar_t* ServiceName = nullptr;
            UNICODE_STRING Node = {};
            UNICODE_STRING Service = {};
            ADDRINFOEXW Hints = {};
            PADDRINFOEXW Result = nullptr;
        };

        void DeleteResolveRequestContext(_In_opt_ void* context) noexcept
        {
            auto* request = static_cast<ResolveRequestContext*>(context);
            if (request == nullptr) {
                return;
            }

            delete[] request->NodeName;
            delete[] request->ServiceName;
            delete request;
        }

        _Must_inspect_result_
        SIZE_T WideStringLength(_In_z_ const wchar_t* text) noexcept
        {
            if (text == nullptr) {
                return 0;
            }

            SIZE_T length = 0;
            while (text[length] != L'\0') {
                ++length;
            }
            return length;
        }

        _Ret_maybenull_
        wchar_t* AllocateWideStringCopy(_In_z_ const wchar_t* text) noexcept
        {
            const SIZE_T length = WideStringLength(text);
            if (length == 0 || length >= (static_cast<SIZE_T>(MAXUSHORT) / sizeof(wchar_t))) {
                return nullptr;
            }

            wchar_t* copy = new wchar_t[length + 1]();
            if (copy == nullptr) {
                return nullptr;
            }

            RtlCopyMemory(copy, text, length * sizeof(wchar_t));
            copy[length] = L'\0';
            return copy;
        }

        _Must_inspect_result_
        bool ParseTcpPort(
            _In_z_ const wchar_t* serviceName,
            _Out_ USHORT* port) noexcept
        {
            if (serviceName == nullptr || port == nullptr) {
                return false;
            }

            ULONG value = 0;
            for (const wchar_t* current = serviceName; *current != L'\0'; ++current) {
                if (*current < L'0' || *current > L'9') {
                    return false;
                }

                value = (value * 10) + static_cast<ULONG>(*current - L'0');
                if (value > 0xffff) {
                    return false;
                }
            }

            if (value == 0) {
                return false;
            }

            *port = RtlUshortByteSwap(static_cast<USHORT>(value));
            return true;
        }

        _Must_inspect_result_
        int ToSocketAddressFamily(WskAddressFamily addressFamily) noexcept
        {
            switch (addressFamily) {
            case WskAddressFamily::Any:
                return AF_UNSPEC;
            case WskAddressFamily::Ipv4:
                return AF_INET;
            case WskAddressFamily::Ipv6:
                return AF_INET6;
            default:
                return -1;
            }
        }

        _Must_inspect_result_
        bool CopySocketAddress(
            _In_ const ADDRINFOEXW* addressInfo,
            USHORT port,
            _Out_ SOCKADDR_STORAGE* remoteAddress) noexcept
        {
            if (addressInfo == nullptr ||
                addressInfo->ai_addr == nullptr ||
                remoteAddress == nullptr ||
                addressInfo->ai_addrlen > sizeof(*remoteAddress)) {
                return false;
            }

            if (addressInfo->ai_family != AF_INET && addressInfo->ai_family != AF_INET6) {
                return false;
            }

            RtlZeroMemory(remoteAddress, sizeof(*remoteAddress));
            RtlCopyMemory(remoteAddress, addressInfo->ai_addr, addressInfo->ai_addrlen);

            if (remoteAddress->ss_family == AF_INET) {
                reinterpret_cast<SOCKADDR_IN*>(remoteAddress)->sin_port = port;
            }
            else if (remoteAddress->ss_family == AF_INET6) {
                reinterpret_cast<SOCKADDR_IN6*>(remoteAddress)->sin6_port = port;
            }

            return true;
        }
    }

    WskClient::WskClient() noexcept
    {
        clientNpi_.ClientContext = this;
        clientNpi_.Dispatch = &WskClientDispatch;
    }

    WskClient::~WskClient() noexcept
    {
        Shutdown();
    }

    NTSTATUS WskClient::Initialize(ULONG waitTimeoutMilliseconds) noexcept
    {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
            return STATUS_INVALID_DEVICE_STATE;
        }

        if (providerCaptured_) {
            return STATUS_SUCCESS;
        }

        NTSTATUS status = STATUS_SUCCESS;

        if (!registered_) {
            RtlZeroMemory(&registration_, sizeof(registration_));

            status = WskRegister(&clientNpi_, &registration_);
            if (!NT_SUCCESS(status)) {
                return status;
            }

            registered_ = true;
        }

        RtlZeroMemory(&providerNpi_, sizeof(providerNpi_));

        status = WskCaptureProviderNPI(
            &registration_,
            waitTimeoutMilliseconds,
            &providerNpi_);

        if (!NT_SUCCESS(status)) {
            Shutdown();
            return status;
        }

        providerCaptured_ = true;
        return STATUS_SUCCESS;
    }

    void WskClient::Shutdown() noexcept
    {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
            NT_ASSERT(false);
            return;
        }

        if (providerCaptured_) {
            WskReleaseProviderNPI(&registration_);
            providerCaptured_ = false;
            RtlZeroMemory(&providerNpi_, sizeof(providerNpi_));
        }

        if (registered_) {
            WskDeregister(&registration_);
            registered_ = false;
            RtlZeroMemory(&registration_, sizeof(registration_));
        }
    }

    bool WskClient::IsInitialized() const noexcept
    {
        return providerCaptured_ &&
            providerNpi_.Client != nullptr &&
            providerNpi_.Dispatch != nullptr;
    }

    PWSK_CLIENT WskClient::ProviderClient() const noexcept
    {
        return IsInitialized() ? providerNpi_.Client : nullptr;
    }

    const WSK_PROVIDER_DISPATCH* WskClient::ProviderDispatch() const noexcept
    {
        return IsInitialized() ? providerNpi_.Dispatch : nullptr;
    }

    NTSTATUS WskClient::Resolve(
        const wchar_t* nodeName,
        const wchar_t* serviceName,
        SOCKADDR_STORAGE* remoteAddress,
        WskAddressFamily addressFamily) noexcept
    {
        SIZE_T addressCount = 0;
        return ResolveAll(
            nodeName,
            serviceName,
            remoteAddress,
            1,
            &addressCount,
            addressFamily);
    }

    NTSTATUS WskClient::ResolveAll(
        const wchar_t* nodeName,
        const wchar_t* serviceName,
        SOCKADDR_STORAGE* remoteAddresses,
        SIZE_T addressCapacity,
        SIZE_T* addressCount,
        WskAddressFamily addressFamily) noexcept
    {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
            return STATUS_INVALID_DEVICE_STATE;
        }

        if (addressCount != nullptr) {
            *addressCount = 0;
        }

        if (nodeName == nullptr || nodeName[0] == L'\0' ||
            serviceName == nullptr || serviceName[0] == L'\0' ||
            remoteAddresses == nullptr ||
            addressCapacity == 0 ||
            addressCount == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        auto* providerClient = ProviderClient();
        const auto* providerDispatch = ProviderDispatch();
        if (providerClient == nullptr ||
            providerDispatch == nullptr ||
            providerDispatch->WskGetAddressInfo == nullptr ||
            providerDispatch->WskFreeAddressInfo == nullptr) {
            return STATUS_DEVICE_NOT_READY;
        }

        USHORT port = 0;
        if (!ParseTcpPort(serviceName, &port)) {
            return STATUS_INVALID_PARAMETER;
        }

        const int socketAddressFamily = ToSocketAddressFamily(addressFamily);
        if (socketAddressFamily < 0) {
            return STATUS_INVALID_PARAMETER;
        }

        WskSyncIrpContext* context = nullptr;
        NTSTATUS status = WskSyncAllocateIrp(&context);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        auto* request = new ResolveRequestContext();
        if (request == nullptr) {
            WskSyncReleaseUnsubmittedContext(context);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        context->CleanupRoutine = DeleteResolveRequestContext;
        context->CleanupContext = request;

        request->NodeName = AllocateWideStringCopy(nodeName);
        request->ServiceName = AllocateWideStringCopy(serviceName);
        if (request->NodeName == nullptr || request->ServiceName == nullptr) {
            WskSyncReleaseUnsubmittedContext(context);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlInitUnicodeString(&request->Node, request->NodeName);
        RtlInitUnicodeString(&request->Service, request->ServiceName);

        // Microsoft name-resolution providers do not support AI_NUMERICSERV;
        // the library validates the numeric service and patches the port below.
        request->Hints.ai_flags = 0;
        request->Hints.ai_family = socketAddressFamily;
        request->Hints.ai_socktype = SOCK_STREAM;
        request->Hints.ai_protocol = IPPROTO_TCP;

        status = providerDispatch->WskGetAddressInfo(
            providerClient,
            &request->Node,
            &request->Service,
            NS_ALL,
            nullptr,
            &request->Hints,
            &request->Result,
            nullptr,
            nullptr,
            context->Irp);

        status = WskSyncCompleteIrp(status, context, WskOperationTimeoutMilliseconds, nullptr);

        if (!NT_SUCCESS(status)) {
            kprintf("WskGetAddressInfo failed: 0x%08X\r\n", static_cast<ULONG>(status));
            WskSyncReleaseContext(context);
            return status;
        }

        if (request->Result == nullptr) {
            kprintf("WskGetAddressInfo returned no results\r\n");
            WskSyncReleaseContext(context);
            return STATUS_NO_MATCH;
        }

        status = STATUS_NOT_FOUND;
        for (const ADDRINFOEXW* current = request->Result; current != nullptr; current = current->ai_next) {
            if (CopySocketAddress(current, port, &remoteAddresses[*addressCount])) {
                status = STATUS_SUCCESS;
                ++(*addressCount);
                if (*addressCount >= addressCapacity) {
                    break;
                }
            }
        }

        providerDispatch->WskFreeAddressInfo(providerClient, request->Result);
        request->Result = nullptr;
        if (!NT_SUCCESS(status)) {
            kprintf("WskGetAddressInfo returned no AF_INET/AF_INET6 address: 0x%08X\r\n",
                static_cast<ULONG>(status));
        }
        WskSyncReleaseContext(context);
        return status;
    }
}
}
