# h2c 客户端支持设计

## 背景

当前 HTTP/2 客户端只支持 TLS + ALPN 的 `h2` 路径，入口位于 [Http2Client.h](src/KernelHttp/client/Http2Client.h) 和 [Http2Client.cpp](src/KernelHttp/client/Http2Client.cpp)。HTTP/2 连接逻辑位于 [Http2Connection.h](src/KernelHttp/http2/Http2Connection.h) 和 [Http2Connection.cpp](src/KernelHttp/http2/Http2Connection.cpp)，目前读写接口强绑定 `TlsConnection`，因此无法直接复用于明文 h2c。

本次目标是实现 h2c，并补充测试和示例。h2c 同时支持两种启动方式：

1. HTTP/1.1 `Upgrade: h2c`
2. prior knowledge，即 TCP 连接后直接发送 HTTP/2 connection preface

## 目标

- 保留现有 HTTPS + ALPN HTTP/2 行为。
- 增加明文 h2c prior knowledge 支持。
- 增加明文 h2c Upgrade 支持。
- 复用现有 HTTP/2 frame、HPACK、stream 和 flow-control 逻辑。
- 补充用户态测试，覆盖 h2c 请求构造和关键协议逻辑。
- 扩展示例，演示 HTTPS h2、h2c prior knowledge、h2c Upgrade。

## 非目标

- 不实现服务端。
- 不实现 h2c 连接池或多请求复用。
- 不实现自动探测或失败后自动降级。
- 不把 WinHTTP、WinINet、SChannel 引入主路径。
- 不引入临时兜底或回退式架构。

## API 设计

在 `Http2Client` 中增加传输模式枚举：

```cpp
enum class Http2TransportMode
{
    TlsAlpn,
    H2cPriorKnowledge,
    H2cUpgrade
};
```

在 `Http2RequestOptions` 中增加：

```cpp
Http2TransportMode TransportMode = Http2TransportMode::TlsAlpn;
```

参数校验按模式区分：

- `TlsAlpn`
  - 需要 `ServerName`
  - 需要 `ServerNameLength`
  - 需要 `CertificateStore`
  - `:scheme` 使用 `https`
- `H2cPriorKnowledge`
  - 不需要 `CertificateStore`
  - `ServerName` 仅用于默认 `:authority` 时使用；如果未提供 `ServerName`，必须提供 `Authority`
  - `:scheme` 使用 `http`
- `H2cUpgrade`
  - 不需要 `CertificateStore`
  - 需要可用于 HTTP/1.1 `Host` 的 authority 信息
  - `:scheme` 使用 `http`

`Http2Response::NegotiatedAlpn` 在 h2c 模式下保持为空，因为 h2c 不经过 ALPN。

## 连接层设计

当前 `Http2Connection` 的 `Initialize`、`SendRequest`、`Shutdown` 需要同时支持 TLS 和明文 TCP。为避免复制 HTTP/2 逻辑，增加一个轻量传输抽象：

```cpp
class Http2Transport
{
public:
    virtual NTSTATUS Send(const UCHAR* data, SIZE_T length) noexcept = 0;
    virtual NTSTATUS Receive(UCHAR* data, SIZE_T length, SIZE_T* bytesReceived) noexcept = 0;
};
```

实际实现使用两个适配器：

- TLS 适配器：内部持有 `WskSocket` 和 `TlsConnection`，读写走 TLS record。
- 明文适配器：内部持有 `WskSocket`，读写直接走 WSK socket。

`Http2Connection` 对外提供基于传输抽象的新接口：

```cpp
NTSTATUS Initialize(Http2Transport& transport) noexcept;
NTSTATUS SendRequest(Http2Transport& transport, ...) noexcept;
NTSTATUS Shutdown(Http2Transport& transport) noexcept;
```

现有基于 `WskSocket + TlsConnection` 的接口可以改为包装新接口，减少调用方改动，并保持 HTTPS h2 行为不变。

## h2c prior knowledge 流程

1. `Http2Client` 通过 WSK 连接远端地址。
2. 创建明文 HTTP/2 transport。
3. 调用 `Http2Connection::Initialize`。
4. `Initialize` 发送：
   - HTTP/2 connection preface
   - 客户端 SETTINGS
5. 读取服务端 SETTINGS。
6. 发送 SETTINGS ACK。
7. 构造 HTTP/2 请求头，`:scheme` 为 `http`。
8. 发送请求并读取响应。
9. 发送 GOAWAY 并关闭 socket。

## h2c Upgrade 流程

1. `Http2Client` 通过 WSK 连接远端地址。
2. 构造 HTTP/1.1 Upgrade 请求：
   - 请求行使用目标 path。
   - `Host: <authority>`
   - `Connection: Upgrade, HTTP2-Settings`
   - `Upgrade: h2c`
   - `HTTP2-Settings: <base64url(client SETTINGS payload)>`
3. 发送 Upgrade 请求。
4. 读取 HTTP/1.1 响应头。
5. 验证：
   - 状态码为 `101`
   - 存在 `Upgrade: h2c`
   - 响应头完整且未超出缓冲区
6. 进入 HTTP/2 初始化：
   - 发送 connection preface
   - 发送客户端 SETTINGS
   - 读取服务端 SETTINGS
   - 发送 SETTINGS ACK
7. 在 HTTP/2 stream 上发送真正的请求。
8. 读取响应，发送 GOAWAY，关闭 socket。

Upgrade 请求只用于切换协议，不把业务请求主体放入 Upgrade 请求中。真正的 HTTP/2 请求在 `101 Switching Protocols` 后单独发送。

## HTTP2-Settings 编码

h2c Upgrade 需要 `HTTP2-Settings` 头。编码流程：

1. 生成 SETTINGS payload，不包含 9 字节 HTTP/2 frame header。
2. 对 payload 做 base64url 编码。
3. 使用 URL-safe 字符集：
   - `+` 替换为 `-`
   - `/` 替换为 `_`
4. 不添加 `=` padding。

测试需要覆盖空 padding、1 字节余数、2 字节余数和实际 SETTINGS payload。

## 请求头构造

公共 HTTP/2 请求头构造逻辑应提取为 helper，供三种模式复用：

- `:method`
- `:scheme`
- `:path`
- `:authority`
- `user-agent`
- `content-type`
- `content-length`
- `accept-encoding`
- 额外 header

禁止转发 HTTP/2 连接级非法头：

- `connection`
- `keep-alive`
- `proxy-connection`
- `transfer-encoding`
- `upgrade`

h2c Upgrade 自身所需的 HTTP/1.1 `Connection` 和 `Upgrade` 头只存在于 Upgrade 握手，不进入 HTTP/2 header block。

## 错误处理

- 参数缺失返回 `STATUS_INVALID_PARAMETER`。
- 连接、发送、接收失败返回底层 NTSTATUS。
- Upgrade 响应不是 `101` 返回 `STATUS_NOT_SUPPORTED`。
- Upgrade 响应格式非法返回 `STATUS_INVALID_NETWORK_RESPONSE`。
- 缓冲区不足返回 `STATUS_BUFFER_TOO_SMALL`。
- HTTP/2 peer SETTINGS 非法沿用现有 `STATUS_INVALID_NETWORK_RESPONSE`。
- 不做自动降级；调用方通过 `TransportMode` 明确选择行为。

## 测试设计

补充用户态测试，优先放入现有 HTTP/2 测试体系：

1. `HTTP2-Settings` base64url 编码：
   - 不输出 padding
   - 使用 `-` 和 `_`
   - SETTINGS payload 编码结果稳定
2. h2c Upgrade 请求构造：
   - 包含 `Connection: Upgrade, HTTP2-Settings`
   - 包含 `Upgrade: h2c`
   - 包含 `HTTP2-Settings`
   - 包含正确 `Host`
3. Upgrade 响应校验：
   - `101 + Upgrade: h2c` 成功
   - 非 101 失败
   - 缺失 Upgrade 失败
4. 请求头构造：
   - h2c 模式 `:scheme` 为 `http`
   - TLS 模式 `:scheme` 为 `https`
   - HTTP/2 禁止头不会进入 header block

## 示例设计

扩展 HTTP/2 示例结构：

- 保留 HTTPS h2 GET/POST 示例。
- 增加 h2c prior knowledge 示例。
- 增加 h2c Upgrade 示例。

示例输出应包含：

- sample name
- transport mode
- NTSTATUS
- HTTP status code
- header count
- body length
- TLS 模式下的 ALPN，h2c 模式为空

## 构建与验证

实现完成后执行：

1. 用户态测试。
2. Debug 构建。

测试完成后必须进行一次 Debug 构建。