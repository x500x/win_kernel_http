# KernelHttp Lib Example Split Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the current monolithic driver project into `KernelHttpLib.lib` plus a `KernelHttpExample.sys` driver that links the library.

**Architecture:** Keep existing source files under `src/KernelHttp` and classify them by project membership instead of moving directories. Add a static library project for reusable core modules and an example driver project for `DriverEntry.cpp` plus samples. Update the solution and test script so Debug builds produce both artifacts.

**Tech Stack:** Visual Studio C++ MSBuild, WindowsKernelModeDriver10.0 toolset, WDM driver project, static library project, pwsh test script.

---

## Chunk 1: Project Split

### Task 1: Add `KernelHttpLib` static library project

**Files:**
- Create: `src/KernelHttpLib/KernelHttpLib.vcxproj`
- Create: `src/KernelHttpLib/KernelHttpLib.vcxproj.filters`

- [ ] Create a `StaticLibrary` project with Debug/Release x64 and ARM64 configurations.
- [ ] Use `WindowsKernelModeDriver10.0`, `TargetVersion=Windows10`, `/kernel` compatible options, no exceptions, no RTTI, warning level 4, warnings as errors.
- [ ] Set include directories to `..\KernelHttp` and `..\..\third_party\brotli\c\include`.
- [ ] Compile core sources from `..\KernelHttp\client`, `crypto`, `engine`, `http`, `http2`, `khttp`, `net`, `tls`, `websocket`.
- [ ] Compile Brotli C sources with `CompileAsC` and the existing warning suppressions.
- [ ] Add filters matching the current module grouping.

### Task 2: Add `KernelHttpExample` driver project

**Files:**
- Create: `src/KernelHttpExample/KernelHttpExample.vcxproj`
- Create: `src/KernelHttpExample/KernelHttpExample.vcxproj.filters`
- Create: `src/KernelHttpExample/KernelHttpExample.inf`

- [ ] Create a WDM `Driver` project with the same configurations as the current project.
- [ ] Compile `..\KernelHttp\DriverEntry.cpp`.
- [ ] Compile `..\KernelHttp\samples\ExternalTrustStore.cpp`, `HighLevelApiSamples.cpp`, `Http2VerbSamples.cpp`, `HttpVerbSamples.cpp`, and `KhttpSamples.cpp`.
- [ ] Include corresponding headers and `KernelHttpExample.inf`.
- [ ] Use include directories `..\KernelHttp` and Brotli include path.
- [ ] Add a `ProjectReference` to `..\KernelHttpLib\KernelHttpLib.vcxproj`.
- [ ] Preserve driver link dependencies `netio.lib;ksecdd.lib`.
- [ ] Output `KernelHttpExample.sys` to `$(SolutionDir)$(Platform)\$(Configuration)\`.

### Task 3: Update solution

**Files:**
- Modify: `KernelHttp.sln`
- Delete: `src/KernelHttp/KernelHttp.vcxproj`
- Delete: `src/KernelHttp/KernelHttp.vcxproj.filters`
- Delete: `src/KernelHttp/KernelHttp.inf`

- [ ] Replace the single `KernelHttp` project with `KernelHttpLib` and `KernelHttpExample`.
- [ ] Add project configuration mappings for Debug/Release x64 and ARM64.
- [ ] Add a project dependency so `KernelHttpExample` depends on `KernelHttpLib`.
- [ ] Keep solution format and Visual Studio version unchanged.
- [ ] Remove stale single-driver project files from `src/KernelHttp`.

## Chunk 2: Tooling And Verification

### Task 4: Update test/build script for renamed driver output

**Files:**
- Modify: `tests/integration/https_smoke.ps1`

- [ ] Change default `ServiceName` to `KernelHttpExample` or keep service name stable if only binary path changes.
- [ ] Change default smoke candidate path from `KernelHttp.sys` to `KernelHttpExample.sys`.
- [ ] Keep host regression source lists unchanged.
- [ ] Keep MSBuild invocation on `KernelHttp.sln`.

### Task 5: Validate project XML and source membership

**Files:**
- Inspect: `src/KernelHttpLib/KernelHttpLib.vcxproj`
- Inspect: `src/KernelHttpExample/KernelHttpExample.vcxproj`
- Inspect: `KernelHttp.sln`

- [ ] Verify `DriverEntry.cpp` and `samples` are absent from `KernelHttpLib`.
- [ ] Verify core sources and Brotli are absent from `KernelHttpExample` except through project reference.
- [ ] Verify `KernelHttpExample.inf` is included only in example.

### Task 6: Test and Debug build

**Commands:**
- Run: `pwsh -NoLogo -NoProfile -File .\tests\integration\https_smoke.ps1 -Configuration Debug -Platform x64 -SkipDriverBuild`
- Run: `pwsh -NoLogo -NoProfile -Command '& msbuild.exe .\KernelHttp.sln /m /restore /p:Configuration=Debug /p:Platform=x64'`

- [ ] Run host regression tests through the integration script with driver build skipped.
- [ ] Run Debug x64 MSBuild for the solution.
- [ ] Confirm `x64\Debug\KernelHttpLib.lib` exists.
- [ ] Confirm `x64\Debug\KernelHttpExample.sys` exists.
- [ ] Do not run VM/load smoke tests.
