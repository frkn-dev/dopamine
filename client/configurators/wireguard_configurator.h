#ifndef WIREGUARD_CONFIGURATOR_H
#define WIREGUARD_CONFIGURATOR_H

#include <QObject>
#include <QProcessEnvironment>

#include "configurator_base.h"
#include "core/defs.h"

class WireguardConfigurator : public ConfiguratorBase
{
    Q_OBJECT
public:
    WireguardConfigurator(std::shared_ptr<Settings> settings, QObject *parent = nullptr);

    struct ConnectionData
    {
        QString clientPrivKey; // client private key
        QString clientPubKey;  // client public key
        QString clientIP;      // internal client IP address
        QString serverPubKey;  // tls-auth key
        QString pskKey;        // preshared key
        QString host;          // host ip
        QString port;
    };

    QString processConfigWithLocalSettings(const QPair<QString, QString> &dns, const bool isApiConfig,
                                           QString &protocolConfigString);
    QString processConfigWithExportSettings(const QPair<QString, QString> &dns, const bool isApiConfig,
                                            QString &protocolConfigString);

    static ConnectionData genClientKeys();

    // Derive the X25519 public key (base64) from a base64-encoded private key.
    // Returns an empty string on failure. Lets us keep a client's key pair stable
    // across reconnects when only the private key was persisted in last_config.
    static QString genPublicKeyFromPrivate(const QString &privateKeyBase64);
};

#endif // WIREGUARD_CONFIGURATOR_H
