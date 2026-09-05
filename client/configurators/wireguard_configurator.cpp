#include "wireguard_configurator.h"

#include <QDebug>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "containers/containers_defs.h"
#include "settings.h"
#include "utilities.h"

WireguardConfigurator::WireguardConfigurator(std::shared_ptr<Settings> settings, QObject *parent)
    : ConfiguratorBase(settings, parent)
{
}

WireguardConfigurator::ConnectionData WireguardConfigurator::genClientKeys()
{
    // TODO review
    constexpr size_t EDDSA_KEY_LENGTH = 32;

    ConnectionData connData;

    unsigned char buff[EDDSA_KEY_LENGTH];
    int ret = RAND_priv_bytes(buff, EDDSA_KEY_LENGTH);
    if (ret <= 0)
        return connData;

    EVP_PKEY *pKey = EVP_PKEY_new();
    q_check_ptr(pKey);
    pKey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, &buff[0], EDDSA_KEY_LENGTH);

    size_t keySize = EDDSA_KEY_LENGTH;

    // save private key
    unsigned char priv[EDDSA_KEY_LENGTH];
    EVP_PKEY_get_raw_private_key(pKey, priv, &keySize);
    connData.clientPrivKey = QByteArray::fromRawData((char *)priv, keySize).toBase64();

    // save public key
    unsigned char pub[EDDSA_KEY_LENGTH];
    EVP_PKEY_get_raw_public_key(pKey, pub, &keySize);
    connData.clientPubKey = QByteArray::fromRawData((char *)pub, keySize).toBase64();

    return connData;
}

QString WireguardConfigurator::genPublicKeyFromPrivate(const QString &privateKeyBase64)
{
    constexpr int X25519_KEY_LENGTH = 32;

    const QByteArray priv = QByteArray::fromBase64(privateKeyBase64.toUtf8());
    if (priv.size() != X25519_KEY_LENGTH) {
        return {};
    }

    EVP_PKEY *pKey = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, nullptr,
                                                  reinterpret_cast<const unsigned char *>(priv.constData()),
                                                  X25519_KEY_LENGTH);
    if (pKey == nullptr) {
        return {};
    }

    unsigned char pub[X25519_KEY_LENGTH];
    size_t keySize = X25519_KEY_LENGTH;
    QString result;
    if (EVP_PKEY_get_raw_public_key(pKey, pub, &keySize) > 0 && keySize == static_cast<size_t>(X25519_KEY_LENGTH)) {
        result = QByteArray(reinterpret_cast<char *>(pub), static_cast<int>(keySize)).toBase64();
    }
    EVP_PKEY_free(pKey);
    return result;
}

QString WireguardConfigurator::processConfigWithLocalSettings(const QPair<QString, QString> &dns,
                                                              const bool isApiConfig, QString &protocolConfigString)
{
    processConfigWithDnsSettings(dns, protocolConfigString);

    return protocolConfigString;
}

QString WireguardConfigurator::processConfigWithExportSettings(const QPair<QString, QString> &dns,
                                                               const bool isApiConfig, QString &protocolConfigString)
{
    processConfigWithDnsSettings(dns, protocolConfigString);

    return protocolConfigString;
}
