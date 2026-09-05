#include "vpnConfigurationController.h"

#include <QJsonArray>

#include "core/api/apiUtils.h"

#include "configurators/awg_configurator.h"
#include "configurators/cloak_configurator.h"
#include "configurators/openvpn_configurator.h"
#include "configurators/shadowsocks_configurator.h"
#include "configurators/wireguard_configurator.h"
#include "configurators/xray_configurator.h"

VpnConfigurationsController::VpnConfigurationsController(const std::shared_ptr<Settings> &settings, QObject *parent)
    : QObject { parent }, m_settings(settings)
{
}

QScopedPointer<ConfiguratorBase> VpnConfigurationsController::createConfigurator(const Proto protocol)
{
    switch (protocol) {
    case Proto::OpenVpn: return QScopedPointer<ConfiguratorBase>(new OpenVpnConfigurator(m_settings));
    case Proto::ShadowSocks: return QScopedPointer<ConfiguratorBase>(new ShadowSocksConfigurator(m_settings));
    case Proto::Cloak: return QScopedPointer<ConfiguratorBase>(new CloakConfigurator(m_settings));
    case Proto::WireGuard: return QScopedPointer<ConfiguratorBase>(new WireguardConfigurator(m_settings));
    case Proto::Awg: return QScopedPointer<ConfiguratorBase>(new AwgConfigurator(m_settings));
    case Proto::Xray: return QScopedPointer<ConfiguratorBase>(new XrayConfigurator(m_settings));
    case Proto::SSXray: return QScopedPointer<ConfiguratorBase>(new XrayConfigurator(m_settings));
    default: return QScopedPointer<ConfiguratorBase>();
    }
}

QJsonObject VpnConfigurationsController::createVpnConfiguration(const QPair<QString, QString> &dns, const QJsonObject &serverConfig,
                                                                const QJsonObject &containerConfig, const DockerContainer container)
{
    QJsonObject vpnConfiguration {};

    if (ContainerProps::containerService(container) == ServiceType::Other) {
        return vpnConfiguration;
    }

    bool isApiConfig = serverConfig.value(config_key::configVersion).toInt();

    for (ProtocolEnumNS::Proto proto : ContainerProps::protocolsForContainer(container)) {
        if (isApiConfig && container == DockerContainer::Cloak && proto == ProtocolEnumNS::Proto::ShadowSocks) {
            continue;
        }

        QJsonObject protoConfig = containerConfig.value(ProtocolProps::protoToString(proto)).toObject();
        if (protoConfig.isEmpty()) {
            // Backend may key the protocol object by container name ("amnezia-awg")
            // instead of protocol name ("awg"): take the first object holding a config.
            for (auto it = containerConfig.begin(); it != containerConfig.end(); ++it) {
                if (it.value().isObject()
                    && (it.value().toObject().contains(config_key::last_config)
                        || it.value().toObject().contains(config_key::config))) {
                    protoConfig = it.value().toObject();
                    break;
                }
            }
        }

        QString protocolConfigString = protoConfig.value(config_key::last_config).toString();
        if (protocolConfigString.isEmpty()) {
            protocolConfigString = protoConfig.value(config_key::config).toString();
        }
        if (protocolConfigString.isEmpty()) {
            // CDN form: the container object itself holds the protocol config
            // (no protocol sub-object): {container, position, config, last_config, ...}
            protocolConfigString = containerConfig.value(config_key::last_config).toString();
        }
        if (protocolConfigString.isEmpty()) {
            protocolConfigString = containerConfig.value(config_key::config).toString();
        }

        auto configurator = createConfigurator(proto);
        protocolConfigString = configurator->processConfigWithLocalSettings(dns, isApiConfig, protocolConfigString);

        QJsonObject vpnConfigData = QJsonDocument::fromJson(protocolConfigString.toUtf8()).object();

        // Normalize AWG junk parameter names: backend last_config uses long names
        // ("junkPacketCount", "initPacketMagicHeader", ...), the app expects short ones
        // ("Jc", "H1", ...).
        static const QList<QPair<QString, QString>> awgJunkKeyMap = {
            { "junkPacketCount", config_key::junkPacketCount },
            { "junkPacketMinSize", config_key::junkPacketMinSize },
            { "junkPacketMaxSize", config_key::junkPacketMaxSize },
            { "initPacketJunkSize", config_key::initPacketJunkSize },
            { "responsePacketJunkSize", config_key::responsePacketJunkSize },
            { "cookieReplyPacketJunkSize", config_key::cookieReplyPacketJunkSize },
            { "transportPacketJunkSize", config_key::transportPacketJunkSize },
            { "initPacketMagicHeader", config_key::initPacketMagicHeader },
            { "responsePacketMagicHeader", config_key::responsePacketMagicHeader },
            { "underloadPacketMagicHeader", config_key::underloadPacketMagicHeader },
            { "transportPacketMagicHeader", config_key::transportPacketMagicHeader },
            { "specialJunk1", config_key::specialJunk1 },
            { "specialJunk2", config_key::specialJunk2 },
            { "specialJunk3", config_key::specialJunk3 },
            { "specialJunk4", config_key::specialJunk4 },
            { "specialJunk5", config_key::specialJunk5 },
            { "randomTrailers", config_key::randomTrailers },
            { "disableCookies", config_key::disableCookies },
            { "headerProtectionKey", config_key::headerProtectionKey },
            { "contentPaddingAddition", config_key::contentPaddingAddition },
            { "rekeyAfterTime", config_key::rekeyAfterTime },
            { "rekeyTimeout", config_key::rekeyTimeout },
            { "rejectAfterTime", config_key::rejectAfterTime },
            { "keepaliveTimeout", config_key::keepaliveTimeout },
            { "maxHandshakeAttempts", config_key::maxHandshakeAttempts },
        };
        for (const auto &[longName, shortName] : awgJunkKeyMap) {
            if (vpnConfigData.contains(longName) && !vpnConfigData.contains(shortName)) {
                vpnConfigData[shortName] = vpnConfigData.value(longName);
            }
        }
        if (ContainerProps::isAwgContainer(container) || container == DockerContainer::WireGuard) {
            // add mtu for old configs
            if (vpnConfigData[config_key::mtu].toString().isEmpty()) {
                vpnConfigData[config_key::mtu] =
                        ContainerProps::isAwgContainer(container) ? protocols::awg::defaultMtu :
                        protocols::wireguard::defaultMtu;
            }

            // Android (Wireguard.kt) hard-requires client_ip and allowed_ips, but
            // backend last_config payloads may omit them (the INI is the source
            // of truth there) — recover from any INI we have.
            QString iniConfig = vpnConfigData.value(config_key::config).toString();
            if (iniConfig.isEmpty()) {
                iniConfig = protoConfig.value(config_key::config).toString();
            }
            if (iniConfig.isEmpty()) {
                iniConfig = containerConfig.value(config_key::config).toString();
            }

            if (vpnConfigData.value(config_key::client_ip).toString().isEmpty()) {
                for (const auto &line : iniConfig.split("\n")) {
                    if (line.startsWith("Address")) {
                        const auto parts = line.split(" = ");
                        if (parts.size() >= 2 && !parts.at(1).trimmed().isEmpty()) {
                            vpnConfigData[config_key::client_ip] = parts.at(1).trimmed();
                        }
                        break;
                    }
                }
            }

            if (!vpnConfigData.value(config_key::allowed_ips).isArray()) {
                QJsonArray allowedIps;
                for (const auto &line : iniConfig.split("\n")) {
                    if (line.contains("AllowedIPs")) {
                        const auto parts = line.split(" = ");
                        if (parts.size() >= 2) {
                            allowedIps = QJsonArray::fromStringList(parts.at(1).split(", "));
                        }
                        break;
                    }
                }
                if (allowedIps.isEmpty()) {
                    allowedIps = QJsonArray { "0.0.0.0/0", "::/0" };
                }
                vpnConfigData[config_key::allowed_ips] = allowedIps;
            }

            // Backend uses "persistent_keepalive", the app expects "persistent_keep_alive"
            if (vpnConfigData.value(config_key::persistent_keep_alive).isUndefined()
                && vpnConfigData.contains(QStringLiteral("persistent_keepalive"))) {
                vpnConfigData[config_key::persistent_keep_alive] =
                        vpnConfigData.value(QStringLiteral("persistent_keepalive"));
            }

            // AWG 3.1: backend may send RandomTrailers/DisableCookies and the
            // AWG 3.0 device-level keys only inside the INI string — recover
            // them into the JSON the platform layers consume.
            for (const auto &key : { config_key::randomTrailers, config_key::disableCookies,
                                     config_key::headerProtectionKey, config_key::contentPaddingAddition,
                                     config_key::rekeyAfterTime, config_key::rekeyTimeout,
                                     config_key::rejectAfterTime, config_key::keepaliveTimeout,
                                     config_key::maxHandshakeAttempts }) {
                if (!vpnConfigData.value(key).toString().isEmpty()) {
                    continue;
                }
                for (const auto &line : iniConfig.split("\n")) {
                    if (line.startsWith(key)) {
                        const auto parts = line.split(" = ");
                        if (parts.size() >= 2 && !parts.at(1).trimmed().isEmpty()) {
                            vpnConfigData[key] = parts.at(1).trimmed();
                        }
                        break;
                    }
                }
            }
        }

        // Backend xray/hysteria2 payloads carry only outbounds; the local socks inbound
        // is a client-side concern. Android (Xray.kt) and the desktop daemon both expect
        // a complete config with inbounds.
        if (container == DockerContainer::Xray) {
            // The backend sends hysteria2 outbounds in the legacy schema (protocol
            // "hysteria2", settings.servers[], network "udp", generic HTTPS ALPN), which
            // the current amnezia-xray-core rejects. iOS translates them on its side;
            // do it here once so every platform gets the new schema (alpn h3).
            QJsonArray outbounds = vpnConfigData.value(QStringLiteral("outbounds")).toArray();
            bool outboundsChanged = false;
            for (int i = 0; i < outbounds.size(); ++i) {
                const QJsonObject translated = apiUtils::translateLegacyHysteria2Outbound(outbounds.at(i).toObject());
                if (translated != outbounds.at(i).toObject()) {
                    outbounds[i] = translated;
                    outboundsChanged = true;
                }
            }
            if (outboundsChanged) {
                vpnConfigData[QStringLiteral("outbounds")] = outbounds;
            }

            if (!vpnConfigData.contains(QStringLiteral("inbounds"))) {
                QJsonObject inbound;
                inbound[QStringLiteral("listen")] = QStringLiteral("127.0.0.1");
                inbound[QStringLiteral("port")] = QString(protocols::xray::defaultLocalProxyPort).toInt();
                inbound[QStringLiteral("protocol")] = QStringLiteral("socks");
                inbound[QStringLiteral("settings")] = QJsonObject { { QStringLiteral("udp"), true } };
                vpnConfigData[QStringLiteral("inbounds")] = QJsonArray { inbound };
            }
        }

        vpnConfiguration.insert(ProtocolProps::key_proto_config_data(proto), vpnConfigData);
    }

    Proto proto = ContainerProps::defaultProtocol(container);
    vpnConfiguration[config_key::vpnproto] = ProtocolProps::protoToString(proto);

    vpnConfiguration[config_key::dns1] = dns.first;
    vpnConfiguration[config_key::dns2] = dns.second;

    vpnConfiguration[config_key::hostName] = serverConfig.value(config_key::hostName).toString();
    vpnConfiguration[config_key::description] = serverConfig.value(config_key::description).toString();

    vpnConfiguration[config_key::configVersion] = serverConfig.value(config_key::configVersion).toInt();
    // TODO: try to get hostName, port, description for 3rd party configs
    // vpnConfiguration[config_key::port] = ...;

    return vpnConfiguration;
}
