#pragma once

#include "tls/TlsRecord.h"

namespace KernelHttp
{
namespace tls
{
    constexpr SIZE_T TlsRandomLength = 32;
    constexpr SIZE_T TlsMasterSecretLength = 48;
    constexpr SIZE_T TlsVerifyDataLength = 12;
    constexpr SIZE_T TlsMaxKeyBlockLength = 160;

    enum class TlsProtocol : UCHAR
    {
        Tls12,
        Tls13Reserved
    };

    enum class TlsCipherSuite : USHORT
    {
        TlsEcdheRsaWithAes128GcmSha256 = 0xC02F,
        TlsEcdheEcdsaWithAes128GcmSha256 = 0xC02B,
        TlsEcdheRsaWithAes256GcmSha384 = 0xC030,
        TlsEcdheEcdsaWithAes256GcmSha384 = 0xC02C
    };

    enum class TlsHandshakeState : UCHAR
    {
        Idle,
        ClientHelloSent,
        ServerHelloReceived,
        ServerCertificateReceived,
        ServerKeyExchangeReceived,
        CertificateRequestReceived,
        ServerHelloDoneReceived,
        ClientFinishedSent,
        Established,
        Failed
    };

    struct TlsSessionSecrets final
    {
        UCHAR ClientRandom[TlsRandomLength] = {};
        UCHAR ServerRandom[TlsRandomLength] = {};
        UCHAR MasterSecret[TlsMasterSecretLength] = {};
        SIZE_T MasterSecretLength = 0;
        TlsCipherSuite CipherSuite = TlsCipherSuite::TlsEcdheRsaWithAes128GcmSha256;
    };

    struct TlsKeyBlock final
    {
        UCHAR Data[TlsMaxKeyBlockLength] = {};
        SIZE_T Length = 0;
    };

    class TlsContext final
    {
    public:
        TlsContext() noexcept;

        void Reset() noexcept;

        _Must_inspect_result_
        NTSTATUS InitializeClient(_In_ TlsProtocolVersion version) noexcept;

        _Must_inspect_result_
        NTSTATUS SetCipherSuite(TlsCipherSuite cipherSuite) noexcept;

        _Must_inspect_result_
        NTSTATUS SetServerRandom(
            _In_reads_bytes_(TlsRandomLength) const UCHAR* random) noexcept;

        _Must_inspect_result_
        NTSTATUS DeriveMasterSecret(
            _In_reads_bytes_(premasterSecretLength) const UCHAR* premasterSecret,
            SIZE_T premasterSecretLength) noexcept;

        _Must_inspect_result_
        NTSTATUS DeriveKeyBlock(
            _Out_ TlsKeyBlock& keyBlock,
            SIZE_T requiredLength) const noexcept;

        _Must_inspect_result_
        NTSTATUS ConfigureAesGcmStates(
            _In_ const TlsKeyBlock& keyBlock,
            _Out_ TlsAeadCipherState& clientWriteState,
            _Out_ TlsAeadCipherState& serverWriteState) const noexcept;

        TlsProtocolVersion Version() const noexcept;
        TlsHandshakeState State() const noexcept;
        TlsCipherSuite CipherSuite() const noexcept;
        const TlsSessionSecrets& Secrets() const noexcept;

        void SetState(TlsHandshakeState state) noexcept;

    private:
        TlsProtocol protocol_ = TlsProtocol::Tls12;
        TlsProtocolVersion version_ = {};
        TlsHandshakeState state_ = TlsHandshakeState::Idle;
        TlsSessionSecrets secrets_ = {};
    };
}
}
