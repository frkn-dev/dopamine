#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantMap>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "containers/containers_defs.h"
#include "core/defs.h"
#include "secure_qsettings.h"

using namespace amnezia;

class QSettings;

class Settings : public QObject
{
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr);

    ServerCredentials defaultServerCredentials() const;
    ServerCredentials serverCredentials(int index) const;

    QJsonArray serversArray() const
    {
        return QJsonDocument::fromJson(m_settings.value("Servers/serversList").toByteArray()).array();
    }
    void setServersArray(const QJsonArray &servers)
    {
        m_settings.setValue("Servers/serversList", QJsonDocument(servers).toJson());
    }

    // Servers section
    int serversCount() const;
    QJsonObject server(int index) const;
    void addServer(const QJsonObject &server);
    void addServers(const QJsonArray &newServers);
    void removeServer(int index);
    bool editServer(int index, const QJsonObject &server);

    int defaultServerIndex() const
    {
        return m_settings.value("Servers/defaultServerIndex", 0).toInt();
    }
    void setDefaultServer(int index)
    {
        m_settings.setValue("Servers/defaultServerIndex", index);
    }
    QJsonObject defaultServer() const
    {
        return server(defaultServerIndex());
    }

    void setDefaultContainer(int serverIndex, DockerContainer container);
    DockerContainer defaultContainer(int serverIndex) const;
    QString defaultContainerName(int serverIndex) const;

    QMap<DockerContainer, QJsonObject> containers(int serverIndex) const;
    void setContainers(int serverIndex, const QMap<DockerContainer, QJsonObject> &containers);

    QJsonObject containerConfig(int serverIndex, DockerContainer container);
    void setContainerConfig(int serverIndex, DockerContainer container, const QJsonObject &config);
    void removeContainerConfig(int serverIndex, DockerContainer container);

    QJsonObject protocolConfig(int serverIndex, DockerContainer container, Proto proto);
    void setProtocolConfig(int serverIndex, DockerContainer container, Proto proto, const QJsonObject &config);

    void clearLastConnectionConfig(int serverIndex, DockerContainer container, Proto proto = Proto::Any);

    bool haveAuthData(int serverIndex) const;
    QString nextAvailableServerName() const;

    // App settings section
    bool isAutoConnect() const
    {
        return m_settings.value("Conf/autoConnect", false).toBool();
    }
    void setAutoConnect(bool enabled)
    {
        m_settings.setValue("Conf/autoConnect", enabled);
    }

    bool isStartMinimized() const
    {
        return m_settings.value("Conf/startMinimized", false).toBool();
    }
    void setStartMinimized(bool enabled)
    {
        m_settings.setValue("Conf/startMinimized", enabled);
    }

    bool isSaveLogs() const
    {
        return m_settings.value("Conf/saveLogs", false).toBool();
    }
    void setSaveLogs(bool enabled);

    // Клиентский override MTU для WG/AWG. 0 = брать значение из конфига сервера.
    int mtuOverride() const
    {
        return m_settings.value("Conf/mtuOverride", 0).toInt();
    }
    void setMtuOverride(int mtu)
    {
        m_settings.setValue("Conf/mtuOverride", mtu);
        emit mtuOverrideChanged(mtu);
    }

    // Адреса пула, которые пользователь выключил (ключ пула -> список IP).
    // Отключённые пользователем адреса узлов — просто список IP. Раньше хранили
    // "<подписка>|<ip>", и после обновления конфига ключ не совпадал: выключенные
    // адреса «включались» сами. IP — единственное, что стабильно между коннектами.
    QStringList poolDisabledEndpoints() const
    {
        const QStringList raw = m_settings.value("Conf/poolDisabledEndpoints", QStringList()).toStringList();
        QStringList ips;
        for (const QString &entry : raw) {
            // старые записи вида "subscription|ip" — берём только IP
            const int sep = entry.lastIndexOf(QLatin1Char('|'));
            const QString ip = (sep >= 0) ? entry.mid(sep + 1) : entry;
            if (!ip.isEmpty() && !ips.contains(ip)) {
                ips.append(ip);
            }
        }
        return ips;
    }
    void setPoolDisabledEndpoints(const QStringList &entries)
    {
        m_settings.setValue("Conf/poolDisabledEndpoints", entries);
        m_settings.sync();
        emit poolDisabledIpsChanged();
    }

    // Диагностическая трасса (кольцевой буфер в настройках приложения).
    // Пишем через ТОТ ЖЕ экземпляр настроек, что и всё приложение: отдельный
    // QSettings на тот же plist затирал записи при следующем sync.
    void appendDiagTrace(const QString &line)
    {
        QStringList trace = m_settings.value("Conf/diagTrace").toStringList();
        trace.append(QTime::currentTime().toString("HH:mm:ss") + QLatin1Char(' ') + line);
        while (trace.size() > 40) {
            trace.removeFirst();
        }
        m_settings.setValue("Conf/diagTrace", trace);
        m_settings.sync();
    }

    void clearDiagTrace()
    {
        m_settings.remove("Conf/diagTrace");
        m_settings.sync();
    }

    QString diagBuild() const
    {
        return m_settings.value("Conf/diagBuild").toString();
    }

    void setDiagBuildMarker(const QString &value)
    {
        m_settings.setValue("Conf/diagBuild", value);
        m_settings.sync();
    }

    // Бесконечные попытки подключения: если сеть пропала, приложение не сдаётся
    // с ошибкой, а ждёт и переподключается само, пока пользователь не нажмёт «Отмена».
    // Время последнего автоотчёта (при провале подключения) — для троттлинга.
    QString lastAutoReportAt() const
    {
        return m_settings.value("Conf/lastAutoReportAt", QString()).toString();
    }
    void setLastAutoReportAt(const QString &iso)
    {
        m_settings.setValue("Conf/lastAutoReportAt", iso);
    }

    // Разрешить юзеру выключать отдельные адреса пула. По умолчанию нет:
    // в карточке сервера тумблеры эндпойнтов не показываем.
    bool isEndpointTogglesAllowed() const
    {
        return m_settings.value("Conf/endpointTogglesAllowed", false).toBool();
    }
    void setEndpointTogglesAllowed(bool allowed)
    {
        if (isEndpointTogglesAllowed() != allowed) {
            m_settings.setValue("Conf/endpointTogglesAllowed", allowed);
            emit endpointTogglesAllowedChanged();
        }
    }

    // MTU, с которым реально подняли туннель в последний раз (для диагностики).
    int lastAppliedMtu() const
    {
        return m_settings.value("Conf/lastAppliedMtu", 0).toInt();
    }

    // Адрес ноды, к которому реально подключились в последний раз. Нужен отчёту,
    // чтобы пометить в пуле «in use» и не путать с выключенным юзером.
    QString lastConnectedEndpoint() const
    {
        return m_settings.value("Conf/lastConnectedEndpoint").toString();
    }
    // Кэш замеров адресов: переживает перезапуск приложения, чтобы свежая
    // (< 1 мин) проверка не запускалась заново при каждом открытии.
    QString endpointHealthCache() const
    {
        return m_settings.value("Conf.endpointHealthCache").toString();
    }
    void setEndpointHealthCache(const QString &json)
    {
        m_settings.setValue("Conf.endpointHealthCache", json);
        // QSettings пишет лениво: без sync кэш не доживал до перезапуска,
        // и приложение снова меряло всё заново.
        m_settings.sync();
    }
    qint64 lastProbeRunMs() const
    {
        return m_settings.value("Conf.lastProbeRunMs", 0).toLongLong();
    }
    void setLastProbeRunMs(qint64 ms)
    {
        m_settings.setValue("Conf.lastProbeRunMs", ms);
        m_settings.sync();
    }

    // Строка, которую юзер выбрал в списке последней раз (не «по умолчанию»).
    // Восстанавливается при запуске: экран не должен прыгать на дефолтный сервер.
    int selectedServerIndex() const
    {
        return m_settings.value("Conf/selectedServerIndex", -1).toInt();
    }
    void setSelectedServerIndex(int index)
    {
        if (index >= 0) {
            m_settings.setValue("Conf/selectedServerIndex", index);
        }
    }

    // Идентичность выбранной строки: пара uuid соединения + id ноды. Индекс
    // «плывёт» при обновлении подписки, пара — нет.
    QString selectedConnectionUuid() const
    {
        return m_settings.value("Conf/selectedConnectionUuid").toString();
    }
    QString selectedNodeId() const
    {
        return m_settings.value("Conf/selectedNodeId").toString();
    }
    // Пул — отдельная идентичность: у строк пула представительная нода
    // меняется между обновлениями подписки, а страна у членов разная (EU:
    // at/ee/fr/pl). Поэтому для пула ключ — имя пула + протокол.
    QString selectedPool() const
    {
        return m_settings.value("Conf/selectedPool").toString();
    }
    QString selectedProtocol() const
    {
        return m_settings.value("Conf/selectedProtocol").toString();
    }
    void setSelectedIdentity(const QString &connectionUuid, const QString &nodeId,
                             const QString &pool = QString(), const QString &protocol = QString())
    {
        m_settings.setValue("Conf/selectedConnectionUuid", connectionUuid);
        m_settings.setValue("Conf/selectedNodeId", nodeId);
        m_settings.setValue("Conf/selectedPool", pool);
        m_settings.setValue("Conf/selectedProtocol", protocol);
    }

    // Строка, к которой реально подключены (для показа в UI после перезапуска:
    // иначе главный экран показывает выбранный сервер, а туннель живёт на другом).
    int lastConnectedServerIndex() const
    {
        return m_settings.value("Conf/lastConnectedServerIndex", -1).toInt();
    }
    void setLastConnectedServerIndex(int index)
    {
        m_settings.setValue("Conf/lastConnectedServerIndex", index);
    }

    void setLastConnectedEndpoint(const QString &ip)
    {
        m_settings.setValue("Conf/lastConnectedEndpoint", ip);
    }

    // Порт того адреса (у пула у каждого IP свой порт): без него «быстрый путь»
    // на реконнекте тыкал бы в дефолтный порт и падал.
    int lastConnectedPort() const
    {
        return m_settings.value("Conf/lastConnectedPort", 0).toInt();
    }
    void setLastConnectedPort(int port)
    {
        m_settings.setValue("Conf/lastConnectedPort", port);
    }

    // Фидбек, который не ушёл из-за отсутствия сети: копим и отправляем позже,
    // иначе юзер пишет в пустоту и думает, что мы его игнорируем.
    QJsonArray pendingFeedback() const
    {
        return QJsonDocument::fromJson(m_settings.value("Conf/pendingFeedback").toString().toUtf8()).array();
    }
    void setPendingFeedback(const QJsonArray &items)
    {
        m_settings.setValue("Conf/pendingFeedback",
                            QString::fromUtf8(QJsonDocument(items).toJson(QJsonDocument::Compact)));
    }

    // История попыток подключения (последние 20): время, адрес, порт, итог.
    // Нужна отчёту — по ней видно, что юзер шесть раз тыкал в разные ноды,
    // а не «просто не работает».
    QJsonArray connectAttempts() const
    {
        return QJsonDocument::fromJson(m_settings.value("Conf/connectAttempts").toString().toUtf8()).array();
    }
    void addConnectAttempt(const QJsonObject &entry)
    {
        QJsonArray attempts = connectAttempts();
        attempts.append(entry);
        while (attempts.size() > 20) {
            attempts.removeFirst();
        }
        m_settings.setValue("Conf/connectAttempts",
                            QString::fromUtf8(QJsonDocument(attempts).toJson(QJsonDocument::Compact)));
    }
    void setLastAppliedMtu(int mtu)
    {
        if (lastAppliedMtu() != mtu) {
            m_settings.setValue("Conf/lastAppliedMtu", mtu);
            emit lastAppliedMtuChanged();
        }
    }

    bool isReconnectUntilCancelled() const
    {
        return m_settings.value("Conf/reconnectUntilCancelled", true).toBool();
    }
    void setReconnectUntilCancelled(bool enabled)
    {
        m_settings.setValue("Conf/reconnectUntilCancelled", enabled);
        emit reconnectUntilCancelledChanged(enabled);
    }

    // Проверка «интернет реально идёт» сразу после подключения: DNS-запрос через
    // туннель. По умолчанию включена — именно она ловит односторонний трафик,
    // когда handshake прошёл, а данные не ходят.
    bool isPostConnectCheckEnabled() const
    {
        return m_settings.value("Conf/postConnectCheck", true).toBool();
    }
    void setPostConnectCheckEnabled(bool enabled)
    {
        m_settings.setValue("Conf/postConnectCheck", enabled);
        emit postConnectCheckChanged(enabled);
    }

    // Последний случай «подключено, но данные не идут» (адрес, причина, время) —
    // для отчёта: иначе в поддержке видно только «соединено».
    QString lastNoDataInfo() const
    {
        return m_settings.value("Conf/lastNoData").toString();
    }
    void setLastNoDataInfo(const QString &info)
    {
        m_settings.setValue("Conf/lastNoData", info);
    }

    // Телеметрия блокировок (отчёт по серверу / скан всех адресов / автосбор сессии).
    // По умолчанию выключена: это исходящие данные о сети пользователя.
    // Живые счётчики трафика текущей сессии: только в памяти, в отчёт попадают
    // как tunnel.transport_bytes. Пишутся из ConnectionController на bytesChanged.
    void setLiveTraffic(qint64 rx, qint64 tx)
    {
        m_liveRxBytes = rx;
        m_liveTxBytes = tx;
    }
    qint64 liveRxBytes() const { return m_liveRxBytes; }
    qint64 liveTxBytes() const { return m_liveTxBytes; }

    // Сводка прошлой сессии (пишется при разрыве, читается в отчёте как prev_session)
    void setPrevSession(const QJsonObject &session)
    {
        m_settings.setValue("Conf.prevSession", QString::fromUtf8(QJsonDocument(session).toJson(QJsonDocument::Compact)));
        m_settings.sync();
    }
    QJsonObject prevSession() const
    {
        const QString raw = m_settings.value("Conf.prevSession").toString();
        return raw.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(raw.toUtf8()).object();
    }

    // Экспериментальные фичи (сейчас — сплит-туннель по сервисам): по умолчанию
    // выключены, включаются тумблером в «Диагностике».
    void clearLastGoodEndpointFor(const QString &key)
    {
        if (key.isEmpty()) {
            return;
        }
        QJsonObject all = lastGoodEndpoints();
        if (!all.contains(key)) {
            return;
        }
        all.remove(key);
        m_settings.setValue("Conf.lastGoodEndpoints",
                            QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
        m_settings.sync();
    }

    QString debugTimeline() const
    {
        return m_settings.value("Conf.dbgTimeline").toString();
    }
    void setDebugTimeline(const QString &value)
    {
        m_settings.setValue("Conf.dbgTimeline", value);
        m_settings.sync();
    }

    void setDebugDial(const QString &value)
    {
        m_settings.setValue("Conf.dbgDial", value);
        m_settings.sync();
    }

    // Память «где строка успешно поднималась» — по идентичности строки
    // (connection_uuid), а не глобально: иначе при переключении RU↔EU память
    // терялась и каждый коннект шёл полным алгоритмом.
    QJsonObject lastGoodEndpoints() const
    {
        return QJsonDocument::fromJson(m_settings.value("Conf.lastGoodEndpoints").toString().toUtf8()).object();
    }
    QJsonObject lastGoodEndpointFor(const QString &key) const
    {
        return lastGoodEndpoints().value(key).toObject();
    }
    void setLastGoodEndpointFor(const QString &key, const QString &ip, int port)
    {
        if (key.isEmpty() || ip.isEmpty()) {
            return;
        }
        QJsonObject all = lastGoodEndpoints();
        QJsonObject entry;
        entry.insert("ip", ip);
        entry.insert("port", port);
        all.insert(key, entry);
        m_settings.setValue("Conf.lastGoodEndpoints",
                            QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
        m_settings.sync();
    }

    bool isExperimentalFeaturesEnabled() const
    {
        return m_settings.value("Conf.experimentalFeatures", false).toBool();
    }
    void setExperimentalFeaturesEnabled(bool enabled)
    {
        m_settings.setValue("Conf.experimentalFeatures", enabled);
        m_settings.sync();
    }

    // Одноразовый вопрос про телеметрию после первого успешного подключения.
    bool isTelemetryPromptShown() const
    {
        return m_settings.value("Conf/telemetryPromptShown", false).toBool();
    }
    void setTelemetryPromptShown(bool shown)
    {
        m_settings.setValue("Conf/telemetryPromptShown", shown);
        m_settings.sync();
    }

    bool isTelemetryEnabled() const
    {
        return m_settings.value("Conf/telemetryEnabled", false).toBool();
    }
    void setTelemetryEnabled(bool enabled)
    {
        m_settings.setValue("Conf/telemetryEnabled", enabled);
        emit telemetryEnabledChanged(enabled);
    }

    QDateTime getLogEnableDate();
    void setLogEnableDate(QDateTime date);

    enum RouteMode {
        VpnAllSites,
        VpnOnlyForwardSites,
        VpnAllExceptSites
    };
    Q_ENUM(RouteMode)

    QString routeModeString(RouteMode mode) const;

    RouteMode routeMode() const;
    void setRouteMode(RouteMode mode) { m_settings.setValue("Conf/routeMode", mode); }

    bool isSitesSplitTunnelingEnabled() const;
    void setSitesSplitTunnelingEnabled(bool enabled);

    QVariantMap vpnSites(RouteMode mode) const
    {
        return m_settings.value("Conf/" + routeModeString(mode)).toMap();
    }
    void setVpnSites(RouteMode mode, const QVariantMap &sites)
    {
        m_settings.setValue("Conf/" + routeModeString(mode), sites);
    }
    bool addVpnSite(RouteMode mode, const QString &site, const QString &ip = "");
    void addVpnSites(RouteMode mode, const QMap<QString, QString> &sites); // map <site, ip>
    QStringList getVpnIps(RouteMode mode) const;
    void removeVpnSite(RouteMode mode, const QString &site);

    void addVpnIps(RouteMode mode, const QStringList &ip);
    void removeVpnSites(RouteMode mode, const QStringList &sites);
    void removeAllVpnSites(RouteMode mode);

    bool useAmneziaDns() const
    {
        return m_settings.value("Conf/useAmneziaDns", true).toBool();
    }
    void setUseAmneziaDns(bool enabled)
    {
        m_settings.setValue("Conf/useAmneziaDns", enabled);
    }

    QString primaryDns() const;
    QString secondaryDns() const;

    // When enabled, these client-side DNS servers replace the values sent by
    // every server. When disabled, the same values remain the fallback for
    // configs that do not carry valid DNS servers of their own.
    bool isDnsOverrideEnabled() const
    {
        return m_settings.value("Conf/dnsOverrideEnabled", false).toBool();
    }
    void setDnsOverrideEnabled(bool enabled)
    {
        m_settings.setValue("Conf/dnsOverrideEnabled", enabled);
        m_settings.sync();
    }

    // QString primaryDns() const { return m_primaryDns; }
    void setPrimaryDns(const QString &primaryDns)
    {
        m_settings.setValue("Conf/primaryDns", primaryDns);
    }

    // QString secondaryDns() const { return m_secondaryDns; }
    void setSecondaryDns(const QString &secondaryDns)
    {
        m_settings.setValue("Conf/secondaryDns", secondaryDns);
    }

    QString serversProtocolFilter() const
    {
        return m_settings.value("Conf/serversProtocolFilter", QString()).toString();
    }
    void setServersProtocolFilter(const QString &filter)
    {
        m_settings.setValue("Conf/serversProtocolFilter", filter);
    }

    QString serversEnvFilter() const
    {
        return m_settings.value("Conf/serversEnvFilter", QString()).toString();
    }
    void setServersEnvFilter(const QString &filter)
    {
        m_settings.setValue("Conf/serversEnvFilter", filter);
    }

    QString serversSubscriptionFilter() const
    {
        return m_settings.value("Conf/serversSubscriptionFilter", QString()).toString();
    }
    void setServersSubscriptionFilter(const QString &filter)
    {
        m_settings.setValue("Conf/serversSubscriptionFilter", filter);
    }

    qint64 lastSubscriptionRefresh() const
    {
        return m_settings.value("Conf/lastSubscriptionRefresh", 0).toLongLong();
    }
    void setLastSubscriptionRefresh(qint64 timestamp)
    {
        m_settings.setValue("Conf/lastSubscriptionRefresh", timestamp);
    }

    // Catalog versioning: the client polls GET v1/catalog/version and reloads
    // the subscription feed only when the hash actually changed.
    QString lastCatalogVersion() const
    {
        return m_settings.value("Conf/lastCatalogVersion").toString();
    }
    void setLastCatalogVersion(const QString &version)
    {
        m_settings.setValue("Conf/lastCatalogVersion", version);
    }
    bool isAutoServerListUpdateEnabled() const
    {
        return m_settings.value("Conf/autoServerListUpdate", true).toBool();
    }
    void setAutoServerListUpdateEnabled(bool enabled)
    {
        m_settings.setValue("Conf/autoServerListUpdate", enabled);
    }

    QVariantMap absentSubscriptionNodes() const
    {
        return m_settings.value("Conf/absentSubscriptionNodes").toMap();
    }
    void setAbsentSubscriptionNodes(const QVariantMap &nodes)
    {
        m_settings.setValue("Conf/absentSubscriptionNodes", nodes);
    }

    // "Auto-select" entry in the server list: on connect the best server is
    // picked by protocol priority + health probe latency (see ConnectionController)
    bool isAutoServerSelection() const
    {
        return m_settings.value("Conf/autoServerSelection", false).toBool();
    }
    void setAutoServerSelection(bool enabled)
    {
        m_settings.setValue("Conf/autoServerSelection", enabled);
    }

    // server list shows only a colored dot by default; this brings back the ping text
    bool isServerPingTextVisible() const
    {
        return m_settings.value("Conf/showServerPingText", false).toBool();
    }
    void setServerPingTextVisible(bool visible)
    {
        m_settings.setValue("Conf/showServerPingText", visible);
    }

    QString splitPresetsVersion() const
    {
        return m_settings.value("Conf/splitPresetsVersion", QString()).toString();
    }
    void setSplitPresetsVersion(const QString &version)
    {
        m_settings.setValue("Conf/splitPresetsVersion", version);
    }
    QString splitPresetsCache() const
    {
        return m_settings.value("Conf/splitPresetsCache", QString()).toString();
    }
    void setSplitPresetsCache(const QString &cache)
    {
        m_settings.setValue("Conf/splitPresetsCache", cache);
    }
    QStringList splitPresetsEnabled() const
    {
        return m_settings.value("Conf/splitPresetsEnabled", QStringList()).toStringList();
    }
    void setSplitPresetsEnabled(const QStringList &enabled)
    {
        m_settings.setValue("Conf/splitPresetsEnabled", enabled);
    }
    // direction of the whole presets section: 1 = via VPN, 2 = bypass VPN (mirrors RouteMode)
    int splitPresetsRouteMode() const
    {
        return m_settings.value("Conf/splitPresetsRouteMode", 1).toInt();
    }
    void setSplitPresetsRouteMode(int mode)
    {
        m_settings.setValue("Conf/splitPresetsRouteMode", mode);
    }

    //    static constexpr char openNicNs5[] = "94.103.153.176";
    //    static constexpr char openNicNs13[] = "144.76.103.143";

    QByteArray backupAppConfig() const
    {
        return m_settings.backupAppConfig();
    }
    bool restoreAppConfig(const QByteArray &cfg)
    {
        return m_settings.restoreAppConfig(cfg);
    }

    QLocale getAppLanguage()
    {
        QString localeStr = m_settings.value("Conf/appLanguage", QLocale::system().name()).toString();
        return QLocale(localeStr);
    };
    void setAppLanguage(QLocale locale)
    {
        m_settings.setValue("Conf/appLanguage", locale.name());
    };

    bool isScreenshotsEnabled() const
    {
        // Always on: the setting was removed from the UI, and old installs may
        // still carry Conf/screenshotsEnabled=false.
        return true;
    }
    void setScreenshotsEnabled(bool enabled)
    {
        m_settings.setValue("Conf/screenshotsEnabled", enabled);
        emit screenshotsEnabledChanged(enabled);
    }

    void clearSettings();

    enum AppsRouteMode {
        VpnAllApps,
        VpnOnlyForwardApps,
        VpnAllExceptApps
    };
    Q_ENUM(AppsRouteMode)

    QString appsRouteModeString(AppsRouteMode mode) const;

    AppsRouteMode getAppsRouteMode() const;
    void setAppsRouteMode(AppsRouteMode mode);

    QVector<InstalledAppInfo> getVpnApps(AppsRouteMode mode) const;
    void setVpnApps(AppsRouteMode mode, const QVector<InstalledAppInfo> &apps);

    bool isAppsSplitTunnelingEnabled() const;
    void setAppsSplitTunnelingEnabled(bool enabled);

    bool isKillSwitchEnabled() const;
    void setKillSwitchEnabled(bool enabled);

    // false (default): LAN is routed around the tunnel; true: LAN goes through the VPN
    bool isRouteLanThroughVpn() const;
    void setRouteLanThroughVpn(bool enabled);

    bool isStrictKillSwitchEnabled() const;
    void setStrictKillSwitchEnabled(bool enabled);

    QString getInstallationUuid(const bool needCreate);

    void resetGatewayEndpoint();
    void setGatewayEndpoint(const QString &endpoint);
    void setDevGatewayEndpoint();
    QString getGatewayEndpoint(bool isTestPurchase = false);
    QString getGatewayEndpointFallback(bool isTestPurchase = false);
    bool isDevGatewayEnv(bool isTestPurchase = false);
    void toggleDevGatewayEnv(bool enabled);

    bool isHomeAdLabelVisible();
    void disableHomeAdLabel();

    bool isPremV1MigrationReminderActive();
    void disablePremV1MigrationReminder();
    
    QStringList allowedDnsServers() const;
    void setAllowedDnsServers(const QStringList &servers);

    /// Local subnets that must bypass the tunnel ("Conf/localSubnetExclusions").
    ///
    /// Stored normalized (see NetworkUtilities::normalizeSubnetExclusions):
    /// CIDR entries of both families, no duplicates, no `/0`, bare addresses
    /// widened to /32 or /128. An empty list means "no local exceptions".
    /// These entries always win over the site/preset split tunneling "via VPN"
    /// choice — they are not a split-tunneling direction, they are a route
    /// around the tunnel.
    QStringList localSubnetExclusions() const;
    void setLocalSubnetExclusions(const QStringList &subnets);

    bool frknDarkMode() const { return m_settings.value("FRKN/darkMode", true).toBool(); }
    void setFrknDarkMode(bool enabled) { m_settings.setValue("FRKN/darkMode", enabled); }

private:
    qint64 m_liveRxBytes = 0;
    qint64 m_liveTxBytes = 0;

signals:
    void saveLogsChanged(bool enabled);
    void screenshotsEnabledChanged(bool enabled);

    void telemetryEnabledChanged(bool enabled);
    void reconnectUntilCancelledChanged(bool enabled);
    void postConnectCheckChanged(bool enabled);
    void mtuOverrideChanged(int mtu);
    void poolDisabledIpsChanged();
    void lastAppliedMtuChanged();
    void endpointTogglesAllowedChanged();
    void serverRemoved(int serverIndex);
    void settingsCleared();

private:
    void setInstallationUuid(const QString &uuid);

    mutable SecureQSettings m_settings;

    QString m_gatewayEndpoint;
};

#endif // SETTINGS_H
