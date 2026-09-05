#include "openvpn_configurator.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTemporaryFile>
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    #include <QGuiApplication>
#else
    #include <QApplication>
#endif

#include "core/networkUtilities.h"
#include "containers/containers_defs.h"
#include "settings.h"
#include "utilities.h"

#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>


OpenVpnConfigurator::OpenVpnConfigurator(std::shared_ptr<Settings> settings, QObject *parent)
    : ConfiguratorBase(settings, parent)
{
}

QString OpenVpnConfigurator::processConfigWithLocalSettings(const QPair<QString, QString> &dns, const bool isApiConfig,
                                                            QString &protocolConfigString)
{
    processConfigWithDnsSettings(dns, protocolConfigString);

    QJsonObject json = QJsonDocument::fromJson(protocolConfigString.toUtf8()).object();
    QString config = json[config_key::config].toString();

    if (!isApiConfig) {
        QRegularExpression regex("redirect-gateway.*");
        config.replace(regex, "");

        // We don't use secondary DNS if primary DNS is AmneziaDNS
        if (dns.first.contains(protocols::dns::amneziaDnsIp)) {
            QRegularExpression dnsRegex("dhcp-option DNS " + dns.second);
            config.replace(dnsRegex, "");
        }

        if (!m_settings->isSitesSplitTunnelingEnabled()) {
            config.append("\nredirect-gateway def1 ipv6 bypass-dhcp\n");
            config.append("block-ipv6\n");
        } else if (m_settings->routeMode() == Settings::VpnOnlyForwardSites) {

               // no redirect-gateway
        } else if (m_settings->routeMode() == Settings::VpnAllExceptSites) {
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS) && !defined(MACOS_NE)
            config.append("\nredirect-gateway ipv6 !ipv4 bypass-dhcp\n");
            // Prevent ipv6 leak
#endif
            config.append("block-ipv6\n");
        }
    }

#ifndef MZ_WINDOWS
    config.replace("block-outside-dns", "");
#endif

#if (defined(MZ_MACOS) || defined(MZ_LINUX))
    QString dnsConf = QString("\nscript-security 2\n"
                              "up %1/update-resolv-conf.sh\n"
                              "down %1/update-resolv-conf.sh\n")
                              .arg(qApp->applicationDirPath());

    config.append(dnsConf);
#endif

    json[config_key::config] = config;
    return QJsonDocument(json).toJson();
}

QString OpenVpnConfigurator::processConfigWithExportSettings(const QPair<QString, QString> &dns, const bool isApiConfig,
                                                             QString &protocolConfigString)
{
    processConfigWithDnsSettings(dns, protocolConfigString);

    QJsonObject json = QJsonDocument::fromJson(protocolConfigString.toUtf8()).object();
    QString config = json[config_key::config].toString();

    QRegularExpression regex("redirect-gateway.*");
    config.replace(regex, "");

    // We don't use secondary DNS if primary DNS is AmneziaDNS
    if (dns.first.contains(protocols::dns::amneziaDnsIp)) {
        QRegularExpression dnsRegex("dhcp-option DNS " + dns.second);
        config.replace(dnsRegex, "");
    }

    config.append("\nredirect-gateway def1 ipv6 bypass-dhcp\n");

    // Prevent ipv6 leak
    config.append("block-ipv6\n");

    // remove block-outside-dns for all exported configs
    config.replace("block-outside-dns", "");

    json[config_key::config] = config;
    return QJsonDocument(json).toJson();
}

OpenVpnConfigurator::ConnectionData OpenVpnConfigurator::createCertRequest()
{
    ConnectionData connData;
    connData.clientId = Utils::getRandomString(32);

    int ret = 0;
    int nVersion = 1;

    QByteArray clientIdUtf8 = connData.clientId.toUtf8();

    EVP_PKEY *pKey = EVP_PKEY_new();
    q_check_ptr(pKey);
    RSA *rsa = RSA_generate_key(2048, RSA_F4, nullptr, nullptr);
    q_check_ptr(rsa);
    EVP_PKEY_assign_RSA(pKey, rsa);

    // 2. set version of x509 req
    X509_REQ *x509_req = X509_REQ_new();
    ret = X509_REQ_set_version(x509_req, nVersion);
    if (ret != 1) {
        qWarning() << "Could not get X509!";
        X509_REQ_free(x509_req);
        EVP_PKEY_free(pKey);
        return connData;
    }

    // 3. set subject of x509 req
    X509_NAME *x509_name = X509_REQ_get_subject_name(x509_req);

    X509_NAME_add_entry_by_txt(x509_name, "C", MBSTRING_ASC, (unsigned char *)"ORG", -1, -1, 0);
    X509_NAME_add_entry_by_txt(x509_name, "O", MBSTRING_ASC, (unsigned char *)"", -1, -1, 0);
    X509_NAME_add_entry_by_txt(x509_name, "CN", MBSTRING_ASC, reinterpret_cast<unsigned char const *>(clientIdUtf8.data()),
                               clientIdUtf8.size(), -1, 0);

    // 4. set public key of x509 req
    ret = X509_REQ_set_pubkey(x509_req, pKey);
    if (ret != 1) {
        qWarning() << "Could not set pubkey!";
        X509_REQ_free(x509_req);
        EVP_PKEY_free(pKey);
        return connData;
    }

    // 5. set sign key of x509 req
    ret = X509_REQ_sign(x509_req, pKey, EVP_sha256()); // return x509_req->signature->length
    if (ret <= 0) {
        qWarning() << "Could not sign request!";
        X509_REQ_free(x509_req);
        EVP_PKEY_free(pKey);
        return connData;
    }

    // save private key
    BIO *bp_private = BIO_new(BIO_s_mem());
    q_check_ptr(bp_private);
    if (PEM_write_bio_PrivateKey(bp_private, pKey, nullptr, nullptr, 0, nullptr, nullptr) != 1) {
        qFatal("PEM_write_bio_PrivateKey");
        EVP_PKEY_free(pKey);
        BIO_free_all(bp_private);
        X509_REQ_free(x509_req);
        return connData;
    }

    const char *buffer = nullptr;
    size_t size = BIO_get_mem_data(bp_private, &buffer);
    q_check_ptr(buffer);
    connData.privKey = QByteArray(buffer, size);
    if (connData.privKey.isEmpty()) {
        qFatal("Failed to generate a random private key");
        EVP_PKEY_free(pKey);
        BIO_free_all(bp_private);
        X509_REQ_free(x509_req);
        return connData;
    }
    BIO_free_all(bp_private);

    // save req
    BIO *bio_req = BIO_new(BIO_s_mem());
    PEM_write_bio_X509_REQ(bio_req, x509_req);

    BUF_MEM *bio_buf;
    BIO_get_mem_ptr(bio_req, &bio_buf);
    connData.request = QByteArray(bio_buf->data, bio_buf->length);
    BIO_free(bio_req);

    EVP_PKEY_free(pKey); // this will also free the rsa key

    return connData;
}
