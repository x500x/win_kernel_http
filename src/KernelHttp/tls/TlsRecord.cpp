#include "tls/TlsRecord.h"

namespace KernelHttp
{
namespace tls
{
    namespace
    {
        constexpr UCHAR TlsMajorVersion = 3;
        constexpr UCHAR Tls10MinorVersion = 1;
        constexpr UCHAR Tls12MinorVersion = 3;

        _Must_inspect_result_
        bool IsValidContentType(UCHAR contentType) noexcept
        {
            return contentType == static_cast<UCHAR>(TlsContentType::ChangeCipherSpec) ||
                contentType == static_cast<UCHAR>(TlsContentType::Alert) ||
                contentType == static_cast<UCHAR>(TlsContentType::Handshake) ||
                contentType == static_cast<UCHAR>(TlsContentType::ApplicationData);
        }

        _Must_inspect_result_
        SIZE_T ReadUint16(_In_reads_bytes_(2) const UCHAR* data) noexcept
        {
            return (static_cast<SIZE_T>(data[0]) << 8) | data[1];
        }

        void WriteUint16(USHORT value, _Out_writes_bytes_(2) UCHAR* data) noexcept
        {
            data[0] = static_cast<UCHAR>((value >> 8) & 0xff);
            data[1] = static_cast<UCHAR>(value & 0xff);
        }

        _Must_inspect_result_
        NTSTATUS ValidatePlaintextRecord(_In_ const TlsPlaintextRecord& record) noexcept
        {
            if (!TlsRecordLayer::IsSupportedVersion(record.Version) ||
                record.FragmentLength > TlsMaxPlaintextLength ||
                (record.Fragment == nullptr && record.FragmentLength != 0)) {
                return STATUS_INVALID_PARAMETER;
            }

            return STATUS_SUCCESS;
        }

        void BuildAesGcmNonce(
            _In_ const TlsAeadCipherState& state,
            _In_reads_bytes_(TlsAesGcmExplicitNonceLength) const UCHAR* explicitNonce,
            _Out_writes_bytes_(TlsAesGcmFixedIvLength + TlsAesGcmExplicitNonceLength) UCHAR* nonce) noexcept
        {
            RtlCopyMemory(nonce, state.FixedIv, TlsAesGcmFixedIvLength);
            RtlCopyMemory(nonce + TlsAesGcmFixedIvLength, explicitNonce, TlsAesGcmExplicitNonceLength);
        }

        void BuildAad(
            unsigned long long sequenceNumber,
            TlsContentType contentType,
            TlsProtocolVersion version,
            SIZE_T plaintextLength,
            _Out_writes_bytes_(13) UCHAR* aad) noexcept
        {
            TlsRecordLayer::WriteSequenceNumber(sequenceNumber, aad);
            aad[8] = static_cast<UCHAR>(contentType);
            aad[9] = version.Major;
            aad[10] = version.Minor;
            WriteUint16(static_cast<USHORT>(plaintextLength), aad + 11);
        }
    }

    void TlsAeadCipherState::Reset() noexcept
    {
        RtlZeroMemory(Key, sizeof(Key));
        KeyLength = 0;
        RtlZeroMemory(FixedIv, sizeof(FixedIv));
        FixedIvLength = 0;
        SequenceNumber = 0;
    }

    bool TlsRecordLayer::IsSupportedVersion(TlsProtocolVersion version) noexcept
    {
        return version.Major == TlsMajorVersion &&
            version.Minor >= Tls10MinorVersion &&
            version.Minor <= Tls12MinorVersion;
    }

    NTSTATUS TlsRecordLayer::Parse(
        const UCHAR* data,
        SIZE_T dataLength,
        TlsRecordView& record) noexcept
    {
        record = {};

        if (data == nullptr) {
            return STATUS_INVALID_PARAMETER;
        }

        if (dataLength < TlsRecordHeaderLength) {
            return STATUS_MORE_PROCESSING_REQUIRED;
        }

        if (!IsValidContentType(data[0])) {
            return STATUS_INVALID_NETWORK_RESPONSE;
        }

        TlsProtocolVersion version = { data[1], data[2] };
        if (!IsSupportedVersion(version)) {
            return STATUS_INVALID_NETWORK_RESPONSE;
        }

        const SIZE_T fragmentLength = ReadUint16(data + 3);
        if (fragmentLength > TlsMaxPlaintextLength + 2048) {
            return STATUS_INVALID_NETWORK_RESPONSE;
        }

        if (dataLength - TlsRecordHeaderLength < fragmentLength) {
            return STATUS_MORE_PROCESSING_REQUIRED;
        }

        record.ContentType = static_cast<TlsContentType>(data[0]);
        record.Version = version;
        record.Fragment = data + TlsRecordHeaderLength;
        record.FragmentLength = fragmentLength;
        record.BytesConsumed = TlsRecordHeaderLength + fragmentLength;

        return STATUS_SUCCESS;
    }

    NTSTATUS TlsRecordLayer::EncodePlaintext(
        const TlsPlaintextRecord& record,
        UCHAR* destination,
        SIZE_T destinationCapacity,
        SIZE_T* bytesWritten) noexcept
    {
        if (bytesWritten != nullptr) {
            *bytesWritten = 0;
        }

        NTSTATUS status = ValidatePlaintextRecord(record);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        const SIZE_T required = TlsRecordHeaderLength + record.FragmentLength;
        if (destination == nullptr || destinationCapacity < required) {
            if (bytesWritten != nullptr) {
                *bytesWritten = required;
            }

            return STATUS_BUFFER_TOO_SMALL;
        }

        destination[0] = static_cast<UCHAR>(record.ContentType);
        destination[1] = record.Version.Major;
        destination[2] = record.Version.Minor;
        WriteUint16(static_cast<USHORT>(record.FragmentLength), destination + 3);

        if (record.FragmentLength != 0) {
            RtlCopyMemory(destination + TlsRecordHeaderLength, record.Fragment, record.FragmentLength);
        }

        if (bytesWritten != nullptr) {
            *bytesWritten = required;
        }

        return STATUS_SUCCESS;
    }

    NTSTATUS TlsRecordLayer::EncodeAlert(
        TlsProtocolVersion version,
        TlsAlertLevel level,
        TlsAlertDescription description,
        UCHAR* destination,
        SIZE_T destinationCapacity,
        SIZE_T* bytesWritten) noexcept
    {
        UCHAR alert[2] = {
            static_cast<UCHAR>(level),
            static_cast<UCHAR>(description)
        };

        TlsPlaintextRecord record = {};
        record.ContentType = TlsContentType::Alert;
        record.Version = version;
        record.Fragment = alert;
        record.FragmentLength = sizeof(alert);

        return EncodePlaintext(record, destination, destinationCapacity, bytesWritten);
    }

    NTSTATUS TlsRecordLayer::ProtectAesGcm(
        const TlsPlaintextRecord& plaintext,
        TlsAeadCipherState& writeState,
        UCHAR* destination,
        SIZE_T destinationCapacity,
        SIZE_T* bytesWritten) noexcept
    {
        if (bytesWritten != nullptr) {
            *bytesWritten = 0;
        }

        NTSTATUS status = ValidatePlaintextRecord(plaintext);
        if (!NT_SUCCESS(status)) {
            return status;
        }

        if (writeState.KeyLength == 0 ||
            writeState.KeyLength > sizeof(writeState.Key) ||
            writeState.FixedIvLength != TlsAesGcmFixedIvLength) {
            return STATUS_INVALID_PARAMETER;
        }

        const SIZE_T encryptedFragmentLength =
            TlsAesGcmExplicitNonceLength + plaintext.FragmentLength + TlsAesGcmTagLength;
        const SIZE_T required = TlsRecordHeaderLength + encryptedFragmentLength;

        if (destination == nullptr || destinationCapacity < required) {
            if (bytesWritten != nullptr) {
                *bytesWritten = required;
            }

            return STATUS_BUFFER_TOO_SMALL;
        }

        destination[0] = static_cast<UCHAR>(plaintext.ContentType);
        destination[1] = plaintext.Version.Major;
        destination[2] = plaintext.Version.Minor;
        WriteUint16(static_cast<USHORT>(encryptedFragmentLength), destination + 3);

        UCHAR* explicitNonce = destination + TlsRecordHeaderLength;
        WriteSequenceNumber(writeState.SequenceNumber, explicitNonce);

        UCHAR nonce[TlsAesGcmFixedIvLength + TlsAesGcmExplicitNonceLength] = {};
        BuildAesGcmNonce(writeState, explicitNonce, nonce);

        UCHAR aad[13] = {};
        BuildAad(
            writeState.SequenceNumber,
            plaintext.ContentType,
            plaintext.Version,
            plaintext.FragmentLength,
            aad);

        crypto::AesGcmKey key = {};
        key.Key = writeState.Key;
        key.KeyLength = writeState.KeyLength;

        crypto::AesGcmParameters parameters = {};
        parameters.Nonce = { nonce, sizeof(nonce) };
        parameters.Aad = { aad, sizeof(aad) };

        UCHAR* ciphertext = explicitNonce + TlsAesGcmExplicitNonceLength;
        UCHAR* tag = ciphertext + plaintext.FragmentLength;
        SIZE_T encryptedLength = 0;

        status = crypto::CngProvider::AesGcmEncrypt(
            key,
            parameters,
            plaintext.Fragment,
            plaintext.FragmentLength,
            ciphertext,
            plaintext.FragmentLength,
            tag,
            TlsAesGcmTagLength,
            &encryptedLength);

        RtlSecureZeroMemory(nonce, sizeof(nonce));
        RtlSecureZeroMemory(aad, sizeof(aad));

        if (!NT_SUCCESS(status)) {
            return status;
        }

        if (encryptedLength != plaintext.FragmentLength) {
            return STATUS_INVALID_NETWORK_RESPONSE;
        }

        IncrementSequence(writeState);

        if (bytesWritten != nullptr) {
            *bytesWritten = required;
        }

        return STATUS_SUCCESS;
    }

    NTSTATUS TlsRecordLayer::UnprotectAesGcm(
        const TlsRecordView& encrypted,
        TlsAeadCipherState& readState,
        UCHAR* plaintext,
        SIZE_T plaintextCapacity,
        TlsMutablePlaintextRecord& output) noexcept
    {
        output = {};

        if (encrypted.Fragment == nullptr ||
            encrypted.FragmentLength < TlsAesGcmExplicitNonceLength + TlsAesGcmTagLength ||
            readState.KeyLength == 0 ||
            readState.KeyLength > sizeof(readState.Key) ||
            readState.FixedIvLength != TlsAesGcmFixedIvLength) {
            return STATUS_INVALID_PARAMETER;
        }

        const SIZE_T plaintextLength =
            encrypted.FragmentLength - TlsAesGcmExplicitNonceLength - TlsAesGcmTagLength;

        if (plaintext == nullptr || plaintextCapacity < plaintextLength) {
            return STATUS_BUFFER_TOO_SMALL;
        }

        const UCHAR* explicitNonce = encrypted.Fragment;
        const UCHAR* ciphertext = encrypted.Fragment + TlsAesGcmExplicitNonceLength;
        const UCHAR* tag = encrypted.Fragment + encrypted.FragmentLength - TlsAesGcmTagLength;

        UCHAR nonce[TlsAesGcmFixedIvLength + TlsAesGcmExplicitNonceLength] = {};
        BuildAesGcmNonce(readState, explicitNonce, nonce);

        UCHAR aad[13] = {};
        BuildAad(
            readState.SequenceNumber,
            encrypted.ContentType,
            encrypted.Version,
            plaintextLength,
            aad);

        crypto::AesGcmKey key = {};
        key.Key = readState.Key;
        key.KeyLength = readState.KeyLength;

        crypto::AesGcmParameters parameters = {};
        parameters.Nonce = { nonce, sizeof(nonce) };
        parameters.Aad = { aad, sizeof(aad) };
        parameters.Tag = { tag, TlsAesGcmTagLength };

        SIZE_T decryptedLength = 0;
        NTSTATUS status = crypto::CngProvider::AesGcmDecrypt(
            key,
            parameters,
            ciphertext,
            plaintextLength,
            plaintext,
            plaintextCapacity,
            &decryptedLength);

        RtlSecureZeroMemory(nonce, sizeof(nonce));
        RtlSecureZeroMemory(aad, sizeof(aad));

        if (!NT_SUCCESS(status)) {
            return status;
        }

        if (decryptedLength != plaintextLength) {
            return STATUS_INVALID_NETWORK_RESPONSE;
        }

        IncrementSequence(readState);

        output.ContentType = encrypted.ContentType;
        output.Version = encrypted.Version;
        output.Fragment = plaintext;
        output.FragmentLength = plaintextLength;

        return STATUS_SUCCESS;
    }

    void TlsRecordLayer::WriteSequenceNumber(unsigned long long sequenceNumber, UCHAR* destination) noexcept
    {
        for (SIZE_T index = 0; index < TlsAesGcmExplicitNonceLength; ++index) {
            const unsigned shift = static_cast<unsigned>((TlsAesGcmExplicitNonceLength - 1 - index) * 8);
            destination[index] = static_cast<UCHAR>((sequenceNumber >> shift) & 0xff);
        }
    }

    void TlsRecordLayer::IncrementSequence(TlsAeadCipherState& state) noexcept
    {
        ++state.SequenceNumber;
    }
}
}
