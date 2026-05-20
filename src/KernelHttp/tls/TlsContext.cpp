#include "tls/TlsContext.h"
#include "tls/TlsHandshake12.h"

namespace KernelHttp
{
namespace tls
{
    namespace
    {
        constexpr SIZE_T Aes128GcmKeyLength = 16;
        constexpr SIZE_T Aes256GcmKeyLength = 32;

        _Must_inspect_result_
        SIZE_T CipherSuiteKeyLength(TlsCipherSuite cipherSuite) noexcept
        {
            switch (cipherSuite) {
            case TlsCipherSuite::TlsEcdheRsaWithAes128GcmSha256:
            case TlsCipherSuite::TlsEcdheEcdsaWithAes128GcmSha256:
                return Aes128GcmKeyLength;
            case TlsCipherSuite::TlsEcdheRsaWithAes256GcmSha384:
            case TlsCipherSuite::TlsEcdheEcdsaWithAes256GcmSha384:
                return Aes256GcmKeyLength;
            default:
                return 0;
            }
        }

        _Must_inspect_result_
        bool IsSupportedCipherSuite(TlsCipherSuite cipherSuite) noexcept
        {
            return CipherSuiteKeyLength(cipherSuite) != 0;
        }
    }

    TlsContext::TlsContext() noexcept
    {
        Reset();
    }

    void TlsContext::Reset() noexcept
    {
        protocol_ = TlsProtocol::Tls12;
        version_ = { 3, 3 };
        state_ = TlsHandshakeState::Idle;
        secrets_ = {};
        secrets_.CipherSuite = TlsCipherSuite::TlsEcdheRsaWithAes128GcmSha256;
    }

    NTSTATUS TlsContext::InitializeClient(TlsProtocolVersion version) noexcept
    {
        if (!TlsRecordLayer::IsSupportedVersion(version) || version.Minor != 3) {
            return STATUS_NOT_SUPPORTED;
        }

        Reset();
        version_ = version;

        NTSTATUS status = crypto::CngProvider::GenerateRandom(secrets_.ClientRandom, sizeof(secrets_.ClientRandom));
        if (!NT_SUCCESS(status)) {
            state_ = TlsHandshakeState::Failed;
            return status;
        }

        state_ = TlsHandshakeState::Idle;
        return STATUS_SUCCESS;
    }

    NTSTATUS TlsContext::SetCipherSuite(TlsCipherSuite cipherSuite) noexcept
    {
        if (!IsSupportedCipherSuite(cipherSuite)) {
            return STATUS_NOT_SUPPORTED;
        }

        secrets_.CipherSuite = cipherSuite;
        return STATUS_SUCCESS;
    }

    NTSTATUS TlsContext::SetServerRandom(const UCHAR* random) noexcept
    {
        if (random == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        RtlCopyMemory(secrets_.ServerRandom, random, sizeof(secrets_.ServerRandom));
        return STATUS_SUCCESS;
    }

    NTSTATUS TlsContext::DeriveMasterSecret(
        const UCHAR* premasterSecret,
        SIZE_T premasterSecretLength) noexcept
    {
        if (premasterSecret == nullptr || premasterSecretLength == 0) {
            return STATUS_INVALID_PARAMETER;
        }

        UCHAR seed[TlsRandomLength * 2] = {};
        RtlCopyMemory(seed, secrets_.ClientRandom, TlsRandomLength);
        RtlCopyMemory(seed + TlsRandomLength, secrets_.ServerRandom, TlsRandomLength);

        NTSTATUS status = TlsHandshake12::Prf(
            TlsHandshake12::PrfHashForCipherSuite(secrets_.CipherSuite),
            premasterSecret,
            premasterSecretLength,
            "master secret",
            seed,
            sizeof(seed),
            secrets_.MasterSecret,
            sizeof(secrets_.MasterSecret));

        RtlSecureZeroMemory(seed, sizeof(seed));

        if (NT_SUCCESS(status)) {
            secrets_.MasterSecretLength = sizeof(secrets_.MasterSecret);
        }

        return status;
    }

    NTSTATUS TlsContext::DeriveKeyBlock(TlsKeyBlock& keyBlock, SIZE_T requiredLength) const noexcept
    {
        keyBlock = {};

        if (secrets_.MasterSecretLength != TlsMasterSecretLength ||
            requiredLength == 0 ||
            requiredLength > sizeof(keyBlock.Data)) {
            return STATUS_INVALID_PARAMETER;
        }

        UCHAR seed[TlsRandomLength * 2] = {};
        RtlCopyMemory(seed, secrets_.ServerRandom, TlsRandomLength);
        RtlCopyMemory(seed + TlsRandomLength, secrets_.ClientRandom, TlsRandomLength);

        NTSTATUS status = TlsHandshake12::Prf(
            TlsHandshake12::PrfHashForCipherSuite(secrets_.CipherSuite),
            secrets_.MasterSecret,
            secrets_.MasterSecretLength,
            "key expansion",
            seed,
            sizeof(seed),
            keyBlock.Data,
            requiredLength);

        RtlSecureZeroMemory(seed, sizeof(seed));

        if (NT_SUCCESS(status)) {
            keyBlock.Length = requiredLength;
        }

        return status;
    }

    NTSTATUS TlsContext::ConfigureAesGcmStates(
        const TlsKeyBlock& keyBlock,
        TlsAeadCipherState& clientWriteState,
        TlsAeadCipherState& serverWriteState) const noexcept
    {
        const SIZE_T keyLength = CipherSuiteKeyLength(secrets_.CipherSuite);
        if (keyLength == 0) {
            return STATUS_NOT_SUPPORTED;
        }

        const SIZE_T requiredLength = (keyLength * 2) + (TlsAesGcmFixedIvLength * 2);
        if (keyBlock.Length < requiredLength) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        clientWriteState.Reset();
        serverWriteState.Reset();

        SIZE_T offset = 0;
        RtlCopyMemory(clientWriteState.Key, keyBlock.Data + offset, keyLength);
        clientWriteState.KeyLength = keyLength;
        offset += keyLength;

        RtlCopyMemory(serverWriteState.Key, keyBlock.Data + offset, keyLength);
        serverWriteState.KeyLength = keyLength;
        offset += keyLength;

        RtlCopyMemory(clientWriteState.FixedIv, keyBlock.Data + offset, TlsAesGcmFixedIvLength);
        clientWriteState.FixedIvLength = TlsAesGcmFixedIvLength;
        offset += TlsAesGcmFixedIvLength;

        RtlCopyMemory(serverWriteState.FixedIv, keyBlock.Data + offset, TlsAesGcmFixedIvLength);
        serverWriteState.FixedIvLength = TlsAesGcmFixedIvLength;

        return STATUS_SUCCESS;
    }

    TlsProtocolVersion TlsContext::Version() const noexcept
    {
        return version_;
    }

    TlsHandshakeState TlsContext::State() const noexcept
    {
        return state_;
    }

    TlsCipherSuite TlsContext::CipherSuite() const noexcept
    {
        return secrets_.CipherSuite;
    }

    const TlsSessionSecrets& TlsContext::Secrets() const noexcept
    {
        return secrets_;
    }

    void TlsContext::SetState(TlsHandshakeState state) noexcept
    {
        state_ = state;
    }
}
}
