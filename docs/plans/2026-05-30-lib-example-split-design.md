# KernelHttp Lib Example Split Design

## Goal

将当前单体驱动工程拆成可复用静态库和示例驱动两部分：

- `KernelHttpLib` 编译核心 HTTP/HTTPS/WebSocket 实现为 `KernelHttpLib.lib`。
- `KernelHttpExample` 编译示例驱动 `.sys`，通过项目引用链接 `KernelHttpLib.lib`。

## Current State

当前 `KernelHttp.sln` 只包含一个 `KernelHttp.vcxproj`。该项目同时编译核心模块、Brotli 源码、`DriverEntry.cpp` 和 `samples/`，所以示例代码与可复用核心代码没有构建边界。

宿主测试通过 `tests/integration/https_smoke.ps1` 直接列出源文件编译，不依赖 `.vcxproj`。本次拆分应尽量保留源码目录和 include 关系，避免让测试脚本承担项目结构重排风险。

## Architecture

新增两个 Visual Studio C++ 项目：

- `src/KernelHttpLib/KernelHttpLib.vcxproj`
  - `ConfigurationType=StaticLibrary`
  - 使用 `WindowsKernelModeDriver10.0` 工具集和现有 `/kernel` 约束。
  - 编译 `src/KernelHttp` 下除 `DriverEntry.cpp` 和 `samples/` 外的核心模块。
  - 编译 `third_party/brotli` 解码源码。

- `src/KernelHttpExample/KernelHttpExample.vcxproj`
  - `ConfigurationType=Driver`
  - 编译 `src/KernelHttp/DriverEntry.cpp` 和 `src/KernelHttp/samples/*.cpp`。
  - 通过 `ProjectReference` 引用 `KernelHttpLib`。
  - 保留现有输出目录模式 `$(SolutionDir)$(Platform)\$(Configuration)\`。

`KernelHttp.sln` 包含 `KernelHttpLib` 和 `KernelHttpExample` 两个项目，并声明 example 依赖 lib。

## File Layout

为降低风险，本次不搬动核心源码文件。源码继续保留在 `src/KernelHttp`，新项目只负责重新分类编译清单。

新增文件：

- `src/KernelHttpLib/KernelHttpLib.vcxproj`
- `src/KernelHttpLib/KernelHttpLib.vcxproj.filters`
- `src/KernelHttpExample/KernelHttpExample.vcxproj`
- `src/KernelHttpExample/KernelHttpExample.vcxproj.filters`
- `src/KernelHttpExample/KernelHttpExample.inf`

调整文件：

- `KernelHttp.sln`
- `tests/integration/https_smoke.ps1`

保留文件：

- `src/KernelHttp/DriverEntry.cpp`
- `src/KernelHttp/KernelHttpConfig.h`
- `src/KernelHttp/samples/*`

移除旧单体项目文件：

- `src/KernelHttp/KernelHttp.vcxproj`
- `src/KernelHttp/KernelHttp.vcxproj.filters`
- `src/KernelHttp/KernelHttp.inf`

## Linking And Dependencies

`KernelHttpExample` 链接 `KernelHttpLib.lib` 后，仍需链接驱动层依赖 `netio.lib;ksecdd.lib`。这些库可以保留在 example 的 Link 配置里，因为最终 `.sys` 在 example 项目产生。

全局 `operator new/delete` 仍放在 `DriverEntry.cpp`。核心库只引用声明，最终由 example 驱动提供定义。这保持当前内核分配语义，也避免在静态库里引入入口级职责。

## Testing

验证分三层：

1. 运行宿主回归测试，不运行 smoke。
2. 运行 Debug x64 MSBuild，确认 lib 与 example driver 均可构建。
3. 检查 solution 与项目依赖，确认 example 先依赖 lib 后链接。

预期产物：

- `x64/Debug/KernelHttpLib.lib`
- `x64/Debug/KernelHttpExample.sys`

## Non Goals

- 不迁移源码目录到 `src/lib` 或 `examples`。
- 不改 HTTP/TLS/WSK 行为。
- 不把 samples 编入核心 lib。
- 不引入兜底构建路径。
