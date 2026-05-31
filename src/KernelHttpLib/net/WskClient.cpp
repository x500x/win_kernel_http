#include <KernelHttp/net/WskClient.h>

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

        _Function_class_(IO_COMPLETION_ROUTINE)
        NTSTATUS WskClientCompletionRoutine(
            _In_ PDEVICE_OBJECT deviceObject,
            _In_ PIRP irp,
            _In_opt_ PVOID context)
        {
            UNREFERENCED_PARAMETER(deviceObject);
            UNREFERENCED_PARAMETER(irp);

            auto* event = static_cast<PKEVENT>(context);
            KeSetEvent(event, IO_NO_INCREMENT, FALSE);
            return STATUS_MORE_PROCESSING_REQUIRED;
        }

        _Must_inspect_result_
        NTSTATUS AllocateSyncIrp(_Outptr_ PIRP* irp, _Out_ PKEVENT event) noexcept
        {
            if (irp == nullptr || event == nullptr) {
                return STATUS_INVALID_PARAMETER;
            }

            *irp = IoAllocateIrp(1, FALSE);
            if (*irp == nullptr) {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            KeInitializeEvent(event, NotificationEvent, FALSE);
            IoSetCompletionRoutine(
                *irp,
                WskClientCompletionRoutine,
                event,
                TRUE,
                TRUE,
                TRUE);

            return STATUS_SUCCESS;
        }

        _Must_inspect_result_
        NTSTATUS CompleteSyncIrp(
            NTSTATUS requestStatus,
            _In_ PIRP irp,
            _In_ PKEVENT event,
            _Inout_ LARGE_INTEGER* timeoutStorage,
            ULONG timeoutMilliseconds = WskOperationTimeoutMilliseconds) noexcept
        {
            if (requestStatus == STATUS_PENDING) {
                if (timeoutStorage == nullptr) {
                    return STATUS_INVALID_PARAMETER;
                }

                timeoutStorage->QuadPart = -static_cast<LONGLONG>(timeoutMilliseconds) * 10 * 1000;

                const NTSTATUS waitStatus = KeWaitForSingleObject(
                    event,
                    Executive,
                    KernelMode,
                    FALSE,
                    timeoutStorage);

                if (waitStatus == STATUS_TIMEOUT) {
                    IoCancelIrp(irp);
                    KeWaitForSingleObject(event, Executive, KernelMode, FALSE, nullptr);
                    return STATUS_IO_TIMEOUT;
                }

                requestStatus = irp->IoStatus.Status;
            }

            return requestStatus;
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

        RtlInitUnicodeString(&nodeString_, nodeName);
        RtlInitUnicodeString(&serviceString_, serviceName);

        USHORT port = 0;
        if (!ParseTcpPort(serviceName, &port)) {
            return STATUS_INVALID_PARAMETER;
        }

        const int socketAddressFamily = ToSocketAddressFamily(addressFamily);
        if (socketAddressFamily < 0) {
            return STATUS_INVALID_PARAMETER;
        }

        RtlZeroMemory(&addressInfoHints_, sizeof(addressInfoHints_));
        // Microsoft name-resolution providers do not support AI_NUMERICSERV;
        // the library validates the numeric service and patches the port below.
        addressInfoHints_.ai_flags = 0;
        addressInfoHints_.ai_family = socketAddressFamily;
        addressInfoHints_.ai_socktype = SOCK_STREAM;
        addressInfoHints_.ai_protocol = IPPROTO_TCP;

        PIRP irp = nullptr;

        NTSTATUS status = AllocateSyncIrp(&irp, &syncEvent_);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        PADDRINFOEXW result = nullptr;
        status = providerDispatch->WskGetAddressInfo(
            providerClient,
            &nodeString_,
            &serviceString_,
            NS_ALL,
            nullptr,
            &addressInfoHints_,
            &result,
            nullptr,
            nullptr,
            irp);

        status = CompleteSyncIrp(status, irp, &syncEvent_, &syncTimeout_);
        IoFreeIrp(irp);

        if (!NT_SUCCESS(status)) {
            kprintf("WskGetAddressInfo failed: 0x%08X\r\n", static_cast<ULONG>(status));
            return status;
        }

        if (result == nullptr) {
            kprintf("WskGetAddressInfo returned no results\r\n");
            return STATUS_NO_MATCH;
        }

        status = STATUS_NOT_FOUND;
        for (const ADDRINFOEXW* current = result; current != nullptr; current = current->ai_next) {
            if (CopySocketAddress(current, port, &remoteAddresses[*addressCount])) {
                status = STATUS_SUCCESS;
                ++(*addressCount);
                if (*addressCount >= addressCapacity) {
                    break;
                }
            }
        }

        providerDispatch->WskFreeAddressInfo(providerClient, result);
        if (!NT_SUCCESS(status)) {
            kprintf("WskGetAddressInfo returned no AF_INET/AF_INET6 address: 0x%08X\r\n",
                static_cast<ULONG>(status));
        }
        return status;
    }
}
}
