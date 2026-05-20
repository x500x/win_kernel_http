#define KERNEL_HTTP_USER_MODE_TEST 1

#include "../src/KernelHttp/tls/TlsContext.h"
#include "../src/KernelHttp/tls/TlsHandshake12.h"
#include "../src/KernelHttp/tls/TlsRecord.h"

#include <stdio.h>
#include <string.h>

using KernelHttp::crypto::HashAlgorithm;
using KernelHttp::tls::TlsAeadCipherState;
using KernelHttp::tls::TlsCipherSuite;
using KernelHttp::tls::TlsClientHelloOptions;
using KernelHttp::tls::TlsContentType;
using KernelHttp::tls::TlsContext;
using KernelHttp::tls::TlsHandshake12;
using KernelHttp::tls::TlsHandshakeMessageView;
using KernelHttp::tls::TlsHandshakeState;
using KernelHttp::tls::TlsHandshakeType;
using KernelHttp::tls::TlsMutablePlaintextRecord;
using KernelHttp::tls::TlsNamedGroup;
using KernelHttp::tls::TlsPlaintextRecord;
using KernelHttp::tls::TlsProtocolVersion;
using KernelHttp::tls::TlsRecordLayer;
using KernelHttp::tls::TlsRecordView;
using KernelHttp::tls::TlsServerHelloView;
using KernelHttp::tls::TlsServerKeyExchangeView;
using KernelHttp::tls::TlsSignatureScheme;
using KernelHttp::tls::TlsSessionSecrets;
using KernelHttp::tls::TlsTranscriptHash;
using KernelHttp::tls::TlsVerifyDataLength;

namespace
{
    bool g_failed = false;

    void Expect(bool condition, const char* message)
    {
        if (!condition) {
            g_failed = true;
            printf("FAIL: %s\n", message);
        }
    }

    void TestPlainRecordRoundTrip()
    {
        const UCHAR body[] = { 1, 2, 3, 4 };
        UCHAR encoded[32] = {};
        SIZE_T written = 0;

        TlsPlaintextRecord plain = {};
        plain.ContentType = TlsContentType::Handshake;
        plain.Version = { 3, 3 };
        plain.Fragment = body;
        plain.FragmentLength = sizeof(body);

        NTSTATUS status = TlsRecordLayer::EncodePlaintext(
            plain,
            encoded,
            sizeof(encoded),
            &written);

        Expect(status == STATUS_SUCCESS, "plaintext record encodes");
        Expect(written == 9, "plaintext record length includes header");
        Expect(encoded[0] == 22 && encoded[3] == 0 && encoded[4] == sizeof(body), "plaintext header is written");

        TlsRecordView parsed = {};
        status = TlsRecordLayer::Parse(encoded, written, parsed);

        Expect(status == STATUS_SUCCESS, "plaintext record parses");
        Expect(parsed.ContentType == TlsContentType::Handshake, "content type parses");
        Expect(parsed.FragmentLength == sizeof(body), "fragment length parses");
        Expect(memcmp(parsed.Fragment, body, sizeof(body)) == 0, "fragment bytes parse");
    }

    void TestRecordNeedsMoreData()
    {
        const UCHAR partial[] = { 22, 3, 3, 0 };
        TlsRecordView parsed = {};
        const NTSTATUS status = TlsRecordLayer::Parse(partial, sizeof(partial), parsed);
        Expect(status == STATUS_MORE_PROCESSING_REQUIRED, "short record asks for more data");
    }

    void TestAesGcmRecordProtection()
    {
        const UCHAR body[] = { 'h', 'e', 'l', 'l', 'o' };
        UCHAR encoded[128] = {};
        UCHAR decoded[32] = {};
        SIZE_T written = 0;

        TlsAeadCipherState writeState = {};
        TlsAeadCipherState readState = {};
        for (SIZE_T index = 0; index < 16; ++index) {
            writeState.Key[index] = static_cast<UCHAR>(index + 1);
            readState.Key[index] = static_cast<UCHAR>(index + 1);
        }

        writeState.KeyLength = 16;
        readState.KeyLength = 16;
        writeState.FixedIvLength = 4;
        readState.FixedIvLength = 4;
        writeState.FixedIv[0] = 0x11;
        readState.FixedIv[0] = 0x11;

        TlsPlaintextRecord plain = {};
        plain.ContentType = TlsContentType::ApplicationData;
        plain.Version = { 3, 3 };
        plain.Fragment = body;
        plain.FragmentLength = sizeof(body);

        NTSTATUS status = TlsRecordLayer::ProtectAesGcm(
            plain,
            writeState,
            encoded,
            sizeof(encoded),
            &written);

        Expect(status == STATUS_SUCCESS, "AES-GCM record protects");
        Expect(writeState.SequenceNumber == 1, "write sequence increments");

        TlsRecordView parsed = {};
        status = TlsRecordLayer::Parse(encoded, written, parsed);
        Expect(status == STATUS_SUCCESS, "protected record parses");

        TlsMutablePlaintextRecord output = {};
        status = TlsRecordLayer::UnprotectAesGcm(
            parsed,
            readState,
            decoded,
            sizeof(decoded),
            output);

        Expect(status == STATUS_SUCCESS, "AES-GCM record unprotects");
        Expect(readState.SequenceNumber == 1, "read sequence increments");
        Expect(output.FragmentLength == sizeof(body), "unprotected length matches");
        Expect(memcmp(output.Fragment, body, sizeof(body)) == 0, "unprotected bytes match");
    }

    void TestClientHello()
    {
        TlsContext context;
        NTSTATUS status = context.InitializeClient({ 3, 3 });
        Expect(status == STATUS_SUCCESS, "TLS context initializes");

        UCHAR message[512] = {};
        SIZE_T written = 0;

        TlsClientHelloOptions options = {};
        options.ServerName = "example.com";
        options.ServerNameLength = strlen(options.ServerName);

        status = TlsHandshake12::EncodeClientHello(
            context,
            options,
            message,
            sizeof(message),
            &written);

        Expect(status == STATUS_SUCCESS, "ClientHello encodes");
        Expect(context.State() == TlsHandshakeState::ClientHelloSent, "ClientHello updates state");
        Expect(written > 40, "ClientHello has body");

        TlsHandshakeMessageView parsed = {};
        status = TlsHandshake12::ParseMessage(message, written, parsed);

        Expect(status == STATUS_SUCCESS, "ClientHello parses as handshake message");
        Expect(parsed.Type == TlsHandshakeType::ClientHello, "ClientHello type parses");
        Expect(parsed.BodyLength == written - 4, "ClientHello length parses");
    }

    void TestParseServerHello()
    {
        TlsContext context;
        NTSTATUS status = context.InitializeClient({ 3, 3 });
        Expect(status == STATUS_SUCCESS, "context initializes for ServerHello");

        UCHAR body[96] = {};
        SIZE_T offset = 0;
        body[offset++] = 3;
        body[offset++] = 3;
        for (SIZE_T index = 0; index < 32; ++index) {
            body[offset++] = static_cast<UCHAR>(0x40 + index);
        }
        body[offset++] = 0;
        body[offset++] = 0xC0;
        body[offset++] = 0x2F;
        body[offset++] = 0;
        body[offset++] = 0;
        body[offset++] = 0;

        TlsHandshakeMessageView message = {};
        message.Type = TlsHandshakeType::ServerHello;
        message.Body = body;
        message.BodyLength = offset;

        TlsServerHelloView serverHello = {};
        status = TlsHandshake12::ParseServerHello(context, message, serverHello);

        Expect(status == STATUS_SUCCESS, "ServerHello parses");
        Expect(context.State() == TlsHandshakeState::ServerHelloReceived, "ServerHello updates state");
        Expect(context.CipherSuite() == TlsCipherSuite::TlsEcdheRsaWithAes128GcmSha256, "cipher suite is selected");
        Expect(serverHello.RandomLength == 32, "server random is exposed");
    }

    void TestParseServerKeyExchange()
    {
        TlsContext context;
        NTSTATUS status = context.InitializeClient({ 3, 3 });
        Expect(status == STATUS_SUCCESS, "context initializes for ServerKeyExchange");

        UCHAR point[65] = {};
        point[0] = 4;
        for (SIZE_T index = 1; index < sizeof(point); ++index) {
            point[index] = static_cast<UCHAR>(index);
        }

        UCHAR body[160] = {};
        SIZE_T offset = 0;
        body[offset++] = 3;
        body[offset++] = 0;
        body[offset++] = static_cast<UCHAR>(TlsNamedGroup::Secp256r1);
        body[offset++] = sizeof(point);
        memcpy(body + offset, point, sizeof(point));
        offset += sizeof(point);
        body[offset++] = 0x04;
        body[offset++] = 0x01;
        body[offset++] = 0;
        body[offset++] = 4;
        body[offset++] = 1;
        body[offset++] = 2;
        body[offset++] = 3;
        body[offset++] = 4;

        TlsHandshakeMessageView message = {};
        message.Type = TlsHandshakeType::ServerKeyExchange;
        message.Body = body;
        message.BodyLength = offset;

        TlsServerKeyExchangeView keyExchange = {};
        status = TlsHandshake12::ParseServerKeyExchange(context, message, keyExchange);

        Expect(status == STATUS_SUCCESS, "ServerKeyExchange parses");
        Expect(context.State() == TlsHandshakeState::ServerKeyExchangeReceived, "ServerKeyExchange updates state");
        Expect(keyExchange.NamedGroup == TlsNamedGroup::Secp256r1, "named group parses");
        Expect(keyExchange.SignatureScheme == TlsSignatureScheme::RsaPkcs1Sha256, "signature scheme parses");
        Expect(keyExchange.EcPointLength == sizeof(point), "EC point parses");
        Expect(keyExchange.SignatureLength == 4, "signature parses");
    }

    void TestEncodeClientKeyExchange()
    {
        const UCHAR publicKey[] = { 4, 1, 2, 3, 4 };
        UCHAR message[32] = {};
        SIZE_T written = 0;

        const NTSTATUS status = TlsHandshake12::EncodeClientKeyExchange(
            publicKey,
            sizeof(publicKey),
            message,
            sizeof(message),
            &written);

        Expect(status == STATUS_SUCCESS, "ClientKeyExchange encodes");
        Expect(written == 4 + 1 + sizeof(publicKey), "ClientKeyExchange length matches");
        Expect(message[0] == static_cast<UCHAR>(TlsHandshakeType::ClientKeyExchange), "ClientKeyExchange type is written");
        Expect(message[4] == sizeof(publicKey), "ClientKeyExchange public key length is written");
    }

    void TestFinishedVerifyData()
    {
        TlsContext context;
        NTSTATUS status = context.InitializeClient({ 3, 3 });
        Expect(status == STATUS_SUCCESS, "TLS context initializes for Finished");

        UCHAR serverRandom[32] = {};
        for (SIZE_T index = 0; index < sizeof(serverRandom); ++index) {
            serverRandom[index] = static_cast<UCHAR>(0x80 + index);
        }

        status = context.SetServerRandom(serverRandom);
        Expect(status == STATUS_SUCCESS, "server random sets");

        const UCHAR premaster[] = {
            0, 1, 2, 3, 4, 5, 6, 7,
            8, 9, 10, 11, 12, 13, 14, 15
        };

        status = context.DeriveMasterSecret(premaster, sizeof(premaster));
        Expect(status == STATUS_SUCCESS, "master secret derives");

        UCHAR transcriptHash[32] = {};
        for (SIZE_T index = 0; index < sizeof(transcriptHash); ++index) {
            transcriptHash[index] = static_cast<UCHAR>(index);
        }

        UCHAR finished[64] = {};
        SIZE_T written = 0;
        status = TlsHandshake12::EncodeFinished(
            context,
            true,
            transcriptHash,
            sizeof(transcriptHash),
            finished,
            sizeof(finished),
            &written);

        Expect(status == STATUS_SUCCESS, "Finished encodes");
        Expect(written == 4 + TlsVerifyDataLength, "Finished length matches");

        status = TlsHandshake12::VerifyFinished(
            context,
            true,
            transcriptHash,
            sizeof(transcriptHash),
            finished + 4,
            TlsVerifyDataLength);

        Expect(status == STATUS_SUCCESS, "Finished verify data validates");
    }

    void TestTranscriptHash()
    {
        TlsTranscriptHash transcript;
        UCHAR digest[48] = {};
        SIZE_T written = 0;

        NTSTATUS status = transcript.Initialize(HashAlgorithm::Sha256);
        Expect(status == STATUS_SUCCESS, "transcript initializes");

        const UCHAR first[] = { 1, 2, 3 };
        const UCHAR second[] = { 4, 5 };

        status = transcript.Update(first, sizeof(first));
        Expect(status == STATUS_SUCCESS, "transcript updates first message");

        status = transcript.Update(second, sizeof(second));
        Expect(status == STATUS_SUCCESS, "transcript updates second message");

        status = transcript.Finish(digest, sizeof(digest), &written);
        Expect(status == STATUS_SUCCESS, "transcript finishes");
        Expect(written == 32, "SHA-256 transcript length is 32");
    }
}

int main()
{
    TestPlainRecordRoundTrip();
    TestRecordNeedsMoreData();
    TestAesGcmRecordProtection();
    TestClientHello();
    TestParseServerHello();
    TestParseServerKeyExchange();
    TestEncodeClientKeyExchange();
    TestFinishedVerifyData();
    TestTranscriptHash();

    if (g_failed) {
        return 1;
    }

    printf("PASS: TLS record tests\n");
    return 0;
}
