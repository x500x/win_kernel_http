#pragma once

#include <KernelHttp/khttp/Types.h>

namespace khttp
{
    _Must_inspect_result_
    NTSTATUS SessionCreate(
        _In_ ::KernelHttp::net::WskClient* wskClient,
        _In_opt_ const SessionConfig* config,
        _Out_ Session** out) noexcept;

    void SessionClose(_In_opt_ Session* session) noexcept;
}
