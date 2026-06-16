#pragma once

#include <KernelHttp/KernelHttpConfig.h>

#if defined(KERNEL_HTTP_USER_MODE_TEST)
#include <stdio.h>
#undef kprintf
#define kprintf(...) printf(__VA_ARGS__)
#else
#undef kprintf
#define kprintf(...) DbgPrintEx(0, 0, KERNEL_HTTP_DRIVER_NAME " : " __VA_ARGS__)
#endif

#undef KHTTP_SAMPLE_LOG
#define KHTTP_SAMPLE_LOG(...) kprintf(__VA_ARGS__)
