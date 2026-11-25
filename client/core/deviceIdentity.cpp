#include "core/deviceIdentity.h"

#include <QByteArray>
#include <QSysInfo>
#include <QUuid>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "secure_qsettings.h"
#include "version.h" // ORGANIZATION_NAME / APPLICATION_NAME: the same Keychain namespace as the app

namespace
{
SecureQSettings &store()
{
    static SecureQSettings settings(ORGANIZATION_NAME, APPLICATION_NAME);
    return settings;
}

constexpr auto kDeviceUid = "peers/device_uid";
constexpr auto kDeviceName = "peers/device_name";
constexpr auto kPrivateKey = "peers/wg_private_key";
constexpr auto kPublicKey = "peers/wg_public_key";

constexpr int kKeyLength = 32;

// X25519 key pair: 32 random bytes clamped by OpenSSL, base64 on the wire.
// Returns false when the CSPRNG fails — a caller must not persist a half pair.
bool generateKeyPair(QString &privateKey, QString &publicKey)
{
    unsigned char raw[kKeyLength];
    if (RAND_priv_bytes(raw, sizeof(raw)) <= 0) {
        return false;
    }

    EVP_PKEY *pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr, raw, sizeof(raw));
    if (pkey == nullptr) {
        return false;
    }

    unsigned char priv[kKeyLength];
    unsigned char pub[kKeyLength];
    size_t privLen = sizeof(priv);
    size_t pubLen = sizeof(pub);
    const bool ok = EVP_PKEY_get_raw_private_key(pkey, priv, &privLen) > 0
                    && EVP_PKEY_get_raw_public_key(pkey, pub, &pubLen) > 0;
    EVP_PKEY_free(pkey);
    if (!ok) {
        return false;
    }

    privateKey = QByteArray(reinterpret_cast<const char *>(priv), static_cast<int>(privLen)).toBase64();
    publicKey = QByteArray(reinterpret_cast<const char *>(pub), static_cast<int>(pubLen)).toBase64();
    return true;
}
} // namespace

QString DeviceIdentity::deviceUid()
{
    const auto stored = store().value(kDeviceUid).toString();
    if (!stored.isEmpty()) {
        return stored;
    }

    // Keychain-backed, so a reinstall on iOS finds it again. When the Keychain is
    // wiped the api simply sees a new device — worse than a reset, never fatal.
    const auto fresh = QUuid::createUuid().toString(QUuid::WithoutBraces);
    store().setValue(kDeviceUid, fresh);
    store().sync();
    return fresh;
}

QString DeviceIdentity::deviceName()
{
    const auto stored = store().value(kDeviceName).toString();
    if (!stored.isEmpty()) {
        return stored;
    }
    auto fallback = QSysInfo::machineHostName();
    if (fallback.isEmpty()) {
        fallback = QStringLiteral("this device");
    }
    return fallback;
}

void DeviceIdentity::setDeviceName(const QString &name)
{
    store().setValue(kDeviceName, name.trimmed());
    store().sync();
}

QString DeviceIdentity::privateKey()
{
    auto &settings = store();
    auto stored = settings.value(kPrivateKey).toString();
    if (!stored.isEmpty()) {
        return stored;
    }

    QString privateKey;
    QString publicKey;
    if (!generateKeyPair(privateKey, publicKey)) {
        return {};
    }

    settings.setValue(kPrivateKey, privateKey);
    settings.setValue(kPublicKey, publicKey);
    settings.sync();
    return privateKey;
}

QString DeviceIdentity::publicKey()
{
    auto &settings = store();
    const auto stored = settings.value(kPublicKey).toString();
    if (!stored.isEmpty()) {
        return stored;
    }

    // privateKey() mints the pair when neither half exists and stores the
    // public one next to it; an install that lost only the public half gets it
    // back from the same call.
    return DeviceIdentity::privateKey().isEmpty() ? QString() : settings.value(kPublicKey).toString();
}
