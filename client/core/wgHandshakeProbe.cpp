#include "wgHandshakeProbe.h"

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

#include <chrono>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#if defined(Q_OS_UNIX)

#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace
{
constexpr size_t kKeyLen = 32;
constexpr size_t kHashLen = 32;
constexpr size_t kAeadTagLen = 16;
constexpr size_t kInitiationSize = 148;
constexpr size_t kResponseSize = 92;
constexpr size_t kCookieReplySize = 64;
constexpr size_t kMac1Offset = 116; // type..timestamp
constexpr quint32 kTypeInitiation = 1;
constexpr quint32 kTypeResponse = 2;
constexpr quint32 kTypeCookieReply = 3;

struct JunkParams
{
    struct Range
    {
        quint32 start = 0;
        quint32 end = 0;
        bool set = false;
    };

    int jc = 0, jmin = 0, jmax = 0;
    int s1 = 0, s2 = 0, s3 = 0;
    Range h1, h2, h3;
    QStringList iSlots; // raw I1..I5 specs, one wire packet per non-empty slot
};

void putLe32(uint8_t *dst, quint32 v)
{
    dst[0] = v & 0xff;
    dst[1] = (v >> 8) & 0xff;
    dst[2] = (v >> 16) & 0xff;
    dst[3] = (v >> 24) & 0xff;
}

quint32 getLe32(const uint8_t *src)
{
    return quint32(src[0]) | (quint32(src[1]) << 8) | (quint32(src[2]) << 16) | (quint32(src[3]) << 24);
}

void putBe32(uint8_t *dst, quint32 v)
{
    dst[0] = (v >> 24) & 0xff;
    dst[1] = (v >> 16) & 0xff;
    dst[2] = (v >> 8) & 0xff;
    dst[3] = v & 0xff;
}

void putBe64(uint8_t *dst, quint64 v)
{
    for (int i = 7; i >= 0; --i) {
        dst[i] = v & 0xff;
        v >>= 8;
    }
}

bool randomBytes(uint8_t *dst, size_t len)
{
    return RAND_bytes(dst, static_cast<int>(len)) == 1;
}

quint32 randomU32(const JunkParams::Range &range)
{
    if (!range.set || range.end <= range.start) {
        return range.start;
    }
    quint32 v = 0;
    randomBytes(reinterpret_cast<uint8_t *>(&v), sizeof(v));
    return range.start + v % (range.end - range.start + 1);
}

bool blake2s256(const uint8_t *a, size_t aLen, const uint8_t *b, size_t bLen, uint8_t out[kHashLen])
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return false;
    }
    unsigned int outLen = 0;
    const bool ok = EVP_DigestInit_ex(ctx, EVP_blake2s256(), nullptr) == 1
            && (!aLen || EVP_DigestUpdate(ctx, a, aLen) == 1) && (!bLen || EVP_DigestUpdate(ctx, b, bLen) == 1)
            && EVP_DigestFinal_ex(ctx, out, &outLen) == 1 && outLen == kHashLen;
    EVP_MD_CTX_free(ctx);
    return ok;
}

bool hmacBlake2s(const uint8_t key[kKeyLen], const uint8_t *data, size_t dataLen, uint8_t out[kHashLen])
{
    bool ok = false;
    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    EVP_MAC_CTX *ctx = mac ? EVP_MAC_CTX_new(mac) : nullptr;
    if (ctx) {
        char digest[] = "BLAKE2S-256";
        OSSL_PARAM params[] = { OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest, 0),
                                OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY, const_cast<uint8_t *>(key), kKeyLen),
                                OSSL_PARAM_construct_end() };
        size_t outLen = 0;
        ok = EVP_MAC_init(ctx, nullptr, 0, params) == 1 && EVP_MAC_update(ctx, data, dataLen) == 1
                && EVP_MAC_final(ctx, out, &outLen, kHashLen) == 1 && outLen == kHashLen;
    }
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return ok;
}

// keyed BLAKE2s with 16-byte output (WG MAC1 — NOT HMAC)
bool blake2sMac128(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t dataLen, uint8_t out[kAeadTagLen])
{
    bool ok = false;
    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "BLAKE2SMAC", nullptr);
    EVP_MAC_CTX *ctx = mac ? EVP_MAC_CTX_new(mac) : nullptr;
    if (ctx) {
        size_t size = kAeadTagLen;
        OSSL_PARAM params[] = { OSSL_PARAM_construct_size_t(OSSL_MAC_PARAM_SIZE, &size),
                                OSSL_PARAM_construct_octet_string(OSSL_MAC_PARAM_KEY, const_cast<uint8_t *>(key), keyLen),
                                OSSL_PARAM_construct_end() };
        size_t outLen = 0;
        ok = EVP_MAC_init(ctx, nullptr, 0, params) == 1 && EVP_MAC_update(ctx, data, dataLen) == 1
                && EVP_MAC_final(ctx, out, &outLen, kAeadTagLen) == 1 && outLen == kAeadTagLen;
    }
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    return ok;
}

// WG KDFn: t0 = HMAC(chainKey, input); Ti = HMAC(t0, T(i-1) || i)
int kdf(uint8_t chainKey[kHashLen], uint8_t out[][kHashLen], int n, const uint8_t *input, size_t inputLen)
{
    uint8_t t0[kHashLen];
    if (!hmacBlake2s(chainKey, input, inputLen, t0)) {
        return 0;
    }
    uint8_t prev[kHashLen];
    for (int i = 0; i < n; ++i) {
        uint8_t buf[kHashLen + 1];
        if (i == 0) {
            buf[0] = static_cast<uint8_t>(i + 1);
            if (!hmacBlake2s(t0, buf, 1, out[i])) {
                return 0;
            }
        } else {
            memcpy(buf, prev, kHashLen);
            buf[kHashLen] = static_cast<uint8_t>(i + 1);
            if (!hmacBlake2s(t0, buf, kHashLen + 1, out[i])) {
                return 0;
            }
        }
        memcpy(prev, out[i], kHashLen);
    }
    memcpy(chainKey, out[0], kHashLen);
    return n;
}

bool aeadSeal(const uint8_t key[kKeyLen], const uint8_t *plain, size_t plainLen, const uint8_t ad[kHashLen],
              uint8_t *out) // out = ciphertext || 16-byte tag
{
    bool ok = false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx) {
        static const uint8_t nonce[12] = { 0 };
        int len = 0, total = 0;
        ok = EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, key, nonce) == 1
                && EVP_EncryptUpdate(ctx, nullptr, &len, ad, kHashLen) == 1
                && EVP_EncryptUpdate(ctx, out, &len, plain, static_cast<int>(plainLen)) == 1
                && (total = len, EVP_EncryptFinal_ex(ctx, out + total, &len) == 1)
                && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, kAeadTagLen, out + plainLen) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

bool aeadOpen(const uint8_t key[kKeyLen], const uint8_t *cipher, size_t cipherLen, const uint8_t ad[kHashLen],
              uint8_t *out) // cipher = ciphertext || 16-byte tag
{
    if (cipherLen < kAeadTagLen) {
        return false;
    }
    bool ok = false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (ctx) {
        static const uint8_t nonce[12] = { 0 };
        int len = 0;
        ok = EVP_DecryptInit_ex(ctx, EVP_chacha20_poly1305(), nullptr, key, nonce) == 1
                && EVP_DecryptUpdate(ctx, nullptr, &len, ad, kHashLen) == 1
                && (cipherLen == kAeadTagLen
                    || EVP_DecryptUpdate(ctx, out, &len, cipher, static_cast<int>(cipherLen - kAeadTagLen)) == 1)
                && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, kAeadTagLen,
                                       const_cast<uint8_t *>(cipher + cipherLen - kAeadTagLen))
                        == 1
                && EVP_DecryptFinal_ex(ctx, out, &len) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

EVP_PKEY *x25519FromPrivate(const uint8_t priv[kKeyLen])
{
    return EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, priv, kKeyLen);
}

bool x25519Public(EVP_PKEY *key, uint8_t pub[kKeyLen])
{
    size_t len = kKeyLen;
    return EVP_PKEY_get_raw_public_key(key, pub, &len) == 1 && len == kKeyLen;
}

bool x25519Private(EVP_PKEY *key, uint8_t priv[kKeyLen])
{
    size_t len = kKeyLen;
    return EVP_PKEY_get_raw_private_key(key, priv, &len) == 1 && len == kKeyLen;
}

// X25519 DH; all-zero shared secret means a low-order point (invalid key)
bool x25519Shared(EVP_PKEY *privKey, const uint8_t peerPub[kKeyLen], uint8_t out[kKeyLen])
{
    bool ok = false;
    EVP_PKEY *peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, nullptr, peerPub, kKeyLen);
    EVP_PKEY_CTX *ctx = peer ? EVP_PKEY_CTX_new(privKey, nullptr) : nullptr;
    if (ctx) {
        size_t len = kKeyLen;
        ok = EVP_PKEY_derive_init(ctx) == 1 && EVP_PKEY_derive_set_peer(ctx, peer) == 1
                && EVP_PKEY_derive(ctx, out, &len) == 1 && len == kKeyLen;
    }
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer);
    if (!ok) {
        return false;
    }
    for (size_t i = 0; i < kKeyLen; ++i) {
        if (out[i] != 0) {
            return true;
        }
    }
    return false;
}

bool decodeKey(const QString &b64, uint8_t out[kKeyLen])
{
    if (b64.isEmpty()) {
        memset(out, 0, kKeyLen);
        return true; // optional key (psk)
    }
    const QByteArray raw = QByteArray::fromBase64(b64.toUtf8());
    if (raw.size() != static_cast<int>(kKeyLen)) {
        return false;
    }
    memcpy(out, raw.constData(), kKeyLen);
    return true;
}

JunkParams::Range parseRange(const QString &spec)
{
    JunkParams::Range range;
    if (spec.isEmpty()) {
        return range;
    }
    const QStringList parts = spec.split(QLatin1Char('-'));
    bool ok1 = false, ok2 = true;
    const quint32 start = parts.at(0).toUInt(&ok1);
    quint32 end = start;
    if (parts.size() > 1) {
        end = parts.at(1).toUInt(&ok2);
    }
    if (ok1 && ok2 && end >= start) {
        range.start = start;
        range.end = end;
        range.set = true;
    } else {
        qWarning() << "[HEALTH] wg probe: bad magic header range" << spec;
    }
    return range;
}

JunkParams parseJunkParams(const QJsonObject &json)
{
    JunkParams p;
    auto getInt = [&json](const char *key) {
        return json.value(QLatin1String(key)).toString().toInt();
    };
    p.jc = getInt("Jc");
    p.jmin = getInt("Jmin");
    p.jmax = getInt("Jmax");
    p.s1 = getInt("S1");
    p.s2 = getInt("S2");
    p.s3 = getInt("S3");
    p.h1 = parseRange(json.value(QStringLiteral("H1")).toString());
    p.h2 = parseRange(json.value(QStringLiteral("H2")).toString());
    p.h3 = parseRange(json.value(QStringLiteral("H3")).toString());
    for (const char *key : { "I1", "I2", "I3", "I4", "I5" }) {
        const QString spec = json.value(QLatin1String(key)).toString();
        if (!spec.isEmpty()) {
            p.iSlots.append(spec);
        }
    }
    if (p.jc < 0 || p.jmin < 0 || p.jmax < p.jmin || p.jmax > 1200) {
        p.jc = 0; // nonsense values -> no junk
    }
    return p;
}

// one wire packet per I-slot: concatenated tag outputs (amneziawg-go obf chain)
QByteArray buildJunkPacketFromSpec(const QString &spec)
{
    QByteArray packet;
    static const QRegularExpression tagRe(QStringLiteral("<(\\w+)(?:\\s+([^>]*))?>"));
    QRegularExpressionMatchIterator it = tagRe.globalMatch(spec);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString tag = match.captured(1);
        const QString val = match.captured(2).trimmed();
        if (tag == QLatin1String("r")) {
            const int n = val.toInt();
            if (n > 0 && n <= 1400) {
                const int oldSize = packet.size();
                packet.resize(oldSize + n);
                randomBytes(reinterpret_cast<uint8_t *>(packet.data() + oldSize), n);
            }
        } else if (tag == QLatin1String("b")) {
            QString hex = val;
            if (hex.startsWith(QLatin1String("0x"))) {
                hex.remove(0, 2);
            }
            packet += QByteArray::fromHex(hex.toLatin1());
        } else if (tag == QLatin1String("t")) {
            uint8_t ts[4];
            putBe32(ts, static_cast<quint32>(QDateTime::currentSecsSinceEpoch()));
            packet.append(reinterpret_cast<const char *>(ts), sizeof(ts));
        } else {
            qWarning() << "[HEALTH] wg probe: unknown I-tag" << tag << "in" << spec;
        }
    }
    return packet;
}

struct HandshakeState
{
    uint8_t hash[kHashLen];
    uint8_t chainKey[kHashLen];
    EVP_PKEY *ephemeral = nullptr; // owned
    EVP_PKEY *staticPriv = nullptr; // owned
    uint8_t psk[kKeyLen];
};

// full response crypto verification (Noise_IK consume-response)
bool verifyResponse(const HandshakeState &state, const uint8_t *resp)
{
    const uint8_t *respEphemeral = resp + 12;
    const uint8_t *respEmpty = resp + 44;

    uint8_t hash[kHashLen], chainKey[kHashLen];
    memcpy(hash, state.hash, kHashLen);
    memcpy(chainKey, state.chainKey, kHashLen);

    uint8_t out[3][kHashLen];
    uint8_t shared[kKeyLen];

    if (!blake2s256(hash, kHashLen, respEphemeral, kKeyLen, hash)) {
        return false;
    }
    if (!kdf(chainKey, out, 1, respEphemeral, kKeyLen)) {
        return false;
    }
    if (!x25519Shared(state.ephemeral, respEphemeral, shared) || !kdf(chainKey, out, 1, shared, kKeyLen)) {
        return false;
    }
    if (!x25519Shared(state.staticPriv, respEphemeral, shared) || !kdf(chainKey, out, 1, shared, kKeyLen)) {
        return false;
    }
    if (!kdf(chainKey, out, 3, state.psk, kKeyLen)) {
        return false;
    }
    // out[1] = tau, out[2] = key
    if (!blake2s256(hash, kHashLen, out[1], kHashLen, hash)) {
        return false;
    }
    uint8_t dummy = 0;
    return aeadOpen(out[2], respEmpty, kAeadTagLen, hash, &dummy);
}

} // namespace

int wgProbeHandshakeRTT(const QString &host, quint16 port,
                        const QString &clientPrivKeyB64, const QString &serverPubKeyB64,
                        const QString &pskB64, const QJsonObject &junkParams, int timeoutMs)
{
    uint8_t clientPriv[kKeyLen], serverPub[kKeyLen], psk[kKeyLen];
    if (!decodeKey(clientPrivKeyB64, clientPriv) || !decodeKey(serverPubKeyB64, serverPub)
        || !decodeKey(pskB64, psk)) {
        qWarning() << "[HEALTH] wg probe: bad keys for" << host;
        return -1;
    }

    const JunkParams junk = parseJunkParams(junkParams);

    HandshakeState state;
    state.ephemeral = [] {
        EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
        EVP_PKEY *key = nullptr;
        if (ctx && EVP_PKEY_keygen_init(ctx) == 1) {
            EVP_PKEY_keygen(ctx, &key);
        }
        EVP_PKEY_CTX_free(ctx);
        return key;
    }();
    state.staticPriv = x25519FromPrivate(clientPriv);
    memcpy(state.psk, psk, kKeyLen);
    if (!state.ephemeral || !state.staticPriv) {
        EVP_PKEY_free(state.ephemeral);
        EVP_PKEY_free(state.staticPriv);
        return -1;
    }
    auto freeKeys = [&] {
        EVP_PKEY_free(state.ephemeral);
        EVP_PKEY_free(state.staticPriv);
    };

    // Noise_IK initiation (see amneziawg-go device/noise-protocol.go)
    static const char kConstruction[] = "Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s";
    static const char kIdentifier[] = "WireGuard v1 zx2c4 Jason@zx2c4.com";
    if (!blake2s256(reinterpret_cast<const uint8_t *>(kConstruction), sizeof(kConstruction) - 1, nullptr, 0,
                    state.chainKey)
        || !blake2s256(state.chainKey, kHashLen, reinterpret_cast<const uint8_t *>(kIdentifier),
                       sizeof(kIdentifier) - 1, state.hash)) {
        freeKeys();
        return -1;
    }

    uint8_t msg[kInitiationSize] = { 0 };
    uint8_t ephPub[kKeyLen], clientPub[kKeyLen], shared[kKeyLen];
    uint8_t out[3][kHashLen];
    bool ok = x25519Public(state.ephemeral, ephPub) && x25519Public(state.staticPriv, clientPub);

    // mixHash(remoteStatic)
    ok = ok && blake2s256(state.hash, kHashLen, serverPub, kKeyLen, state.hash);

    // ephemeral
    if (ok) {
        memcpy(msg + 8, ephPub, kKeyLen);
        ok = kdf(state.chainKey, out, 1, ephPub, kKeyLen)
                && blake2s256(state.hash, kHashLen, ephPub, kKeyLen, state.hash);
    }

    // encrypted static
    if (ok) {
        ok = x25519Shared(state.ephemeral, serverPub, shared) && kdf(state.chainKey, out, 2, shared, kKeyLen)
                && aeadSeal(out[1], clientPub, kKeyLen, state.hash, msg + 40)
                && blake2s256(state.hash, kHashLen, msg + 40, kKeyLen + kAeadTagLen, state.hash);
    }

    // encrypted tai64n timestamp
    if (ok) {
        uint8_t timestamp[12];
        const qint64 msecs = QDateTime::currentMSecsSinceEpoch();
        putBe64(timestamp, static_cast<quint64>(msecs / 1000) + 0x400000000000000aULL);
        putBe32(timestamp + 8, static_cast<quint32>(msecs % 1000) * 1000000u);
        ok = x25519Shared(state.staticPriv, serverPub, shared) && kdf(state.chainKey, out, 2, shared, kKeyLen)
                && aeadSeal(out[1], timestamp, sizeof(timestamp), state.hash, msg + 88)
                && blake2s256(state.hash, kHashLen, msg + 88, 12 + kAeadTagLen, state.hash);
    }
    if (!ok) {
        qWarning() << "[HEALTH] wg probe: handshake build failed for" << host;
        freeKeys();
        return -1;
    }

    // sender index + wire type (AWG: random magic header in H1 range)
    quint32 sender = 0;
    randomBytes(reinterpret_cast<uint8_t *>(&sender), sizeof(sender));
    if (sender == 0) {
        sender = 1;
    }
    putLe32(msg, junk.h1.set ? randomU32(junk.h1) : kTypeInitiation);
    putLe32(msg + 4, sender);

    // MAC1 over the wire image [0..116) — includes the (possibly randomized) type
    {
        uint8_t macKey[kHashLen];
        static const char kMac1Label[] = "mac1----";
        ok = blake2s256(reinterpret_cast<const uint8_t *>(kMac1Label), sizeof(kMac1Label) - 1, serverPub, kKeyLen,
                        macKey)
                && blake2sMac128(macKey, kHashLen, msg, kMac1Offset, msg + kMac1Offset);
    }
    if (!ok) {
        freeKeys();
        return -1;
    }
    // MAC2 stays zero (no cookie)

    // resolve
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *addresses = nullptr;
    if (getaddrinfo(host.toUtf8().constData(), QString::number(port).toUtf8().constData(), &hints, &addresses) != 0
        || !addresses) {
        qWarning() << "[HEALTH] wg probe: resolve failed for" << host;
        freeKeys();
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *ai = addresses; ai && sock < 0; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
            continue;
        }
        const int candidate = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (candidate >= 0 && connect(candidate, ai->ai_addr, ai->ai_addrlen) == 0) {
            sock = candidate;
        } else if (candidate >= 0) {
            close(candidate);
        }
    }
    freeaddrinfo(addresses);
    if (sock < 0) {
        qWarning() << "[HEALTH] wg probe: socket failed for" << host;
        freeKeys();
        return -1;
    }

    // datagrams to send: I1..I5 junk, Jc junk, then the initiation (S1 prefix + msg)
    QList<QByteArray> datagrams;
    for (const QString &slot : junk.iSlots) {
        const QByteArray packet = buildJunkPacketFromSpec(slot);
        if (!packet.isEmpty()) {
            datagrams.append(packet);
        }
    }
    for (int i = 0; i < junk.jc; ++i) {
        quint32 n = junk.jmin;
        if (junk.jmax > junk.jmin) {
            quint32 v = 0;
            randomBytes(reinterpret_cast<uint8_t *>(&v), sizeof(v));
            n += v % (junk.jmax - junk.jmin + 1);
        }
        QByteArray packet;
        packet.resize(static_cast<int>(n));
        randomBytes(reinterpret_cast<uint8_t *>(packet.data()), n);
        datagrams.append(packet);
    }
    {
        QByteArray initPacket;
        initPacket.resize(junk.s1 + static_cast<int>(kInitiationSize));
        randomBytes(reinterpret_cast<uint8_t *>(initPacket.data()), junk.s1);
        memcpy(initPacket.data() + junk.s1, msg, kInitiationSize);
        datagrams.append(initPacket);
    }

    auto sendAll = [&]() {
        for (const QByteArray &d : datagrams) {
            send(sock, d.constData(), d.size(), 0);
        }
    };

    sendAll();
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds(timeoutMs);
    const auto retryAt = start + std::chrono::milliseconds(timeoutMs / 2);
    bool retried = false;
    int rttMs = -1;

    while (rttMs < 0) {
        const auto now = std::chrono::steady_clock::now();
        const auto limit = retried ? deadline : retryAt;
        if (now >= limit) {
            if (!retried) {
                // WG retries are normal on lossy links: one retransmit at half timeout
                sendAll();
                retried = true;
                continue;
            }
            break;
        }
        const auto waitUs = std::chrono::duration_cast<std::chrono::microseconds>(limit - now).count();
        struct timeval tv;
        tv.tv_sec = static_cast<long>(waitUs / 1000000);
        tv.tv_usec = static_cast<long>(waitUs % 1000000);
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        uint8_t buf[2048];
        const ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) {
            continue;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                                     .count();

        // response: S2 prefix + 92 bytes; cookie reply: S3 prefix + 64 bytes (also proves liveness)
        if (n == static_cast<ssize_t>(junk.s3 + kCookieReplySize)) {
            const uint8_t *cookie = buf + junk.s3;
            const quint32 type = getLe32(cookie);
            const bool plausible = junk.h3.set ? (type >= junk.h3.start && type <= junk.h3.end) : type == kTypeCookieReply;
            if (plausible && getLe32(cookie + 4) == sender) {
                rttMs = static_cast<int>(elapsed);
                break;
            }
        }
        if (n != static_cast<ssize_t>(junk.s2 + kResponseSize)) {
            continue;
        }
        const uint8_t *resp = buf + junk.s2;
        const quint32 type = getLe32(resp);
        const bool plausible = junk.h2.set ? (type >= junk.h2.start && type <= junk.h2.end) : type == kTypeResponse;
        if (!plausible || getLe32(resp + 8) != sender) {
            continue;
        }
        // spoofed/garbage packet must not fake "online": verify the full handshake
        if (verifyResponse(state, resp)) {
            rttMs = static_cast<int>(elapsed);
            break;
        }
    }

    close(sock);
    freeKeys();
    return rttMs;
}

#else // !Q_OS_UNIX — probe not supported (only called on Android anyway)

int wgProbeHandshakeRTT(const QString &host, quint16 port,
                        const QString &clientPrivKeyB64, const QString &serverPubKeyB64,
                        const QString &pskB64, const QJsonObject &junkParams, int timeoutMs)
{
    Q_UNUSED(host) Q_UNUSED(port) Q_UNUSED(clientPrivKeyB64) Q_UNUSED(serverPubKeyB64) Q_UNUSED(pskB64)
            Q_UNUSED(junkParams) Q_UNUSED(timeoutMs)
    return -1;
}

#endif
