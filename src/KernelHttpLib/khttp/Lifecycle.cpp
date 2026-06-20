#include <KernelHttp/khttp/Lifecycle.h>
#include <KernelHttp/engine/Engine.h>

namespace khttp
{
    void Destroy() noexcept
    {
        (void)::KernelHttp::engine::KhEngineDrainAsync();
    }
}
