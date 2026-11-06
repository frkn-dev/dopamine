import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import SortFilterProxyModel 0.2

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    // Порт текущего сервера — читаем один раз на страницу, а не в каждом делегате.
    property int processedServerPort: ServersModel.getProcessedServerPort()

    // Выключенные адреса живут в SettingsController.poolDisabledIps (Q_PROPERTY
    // с NOTIFY), поэтому карточка обновляется биндингом сама — без вызовов
    // C++-методов из QML, которые в этой сборке оказались недоступны.

    property list<QtObject> labelsModel: [
        statusObject
    ]

    QtObject {
        id: statusObject

        readonly property string title: qsTr("Subscription Status")
        readonly property string contentKey: "subscriptionStatus"
        readonly property string objectImageSource: "qrc:/images/controls/info.svg"
        readonly property bool isRichText: true
    }

    property var processedServer
    property bool isSharedConnection: false
    property bool canShareServer: false

    // getCurrentServerClientIp() / getCurrentServerMtu() read the per-server config,
    // which gateway servers install lazily on (re)connect. Bump this so those rows
    // re-evaluate instead of being computed once when the page opens.
    // Строка, для которой открыта карточка. Отчёт собираем именно по ней:
    // глобальный processedIndex сбрасывается кнопкой Connect на сервер по
    // умолчанию, и отчёт уезжал по чужой строке.
    property int openedForIndex: -1
    property int configRevision: 0

    // После отключения VPN пробы снова имеют смысл — сразу меряем, чтобы в
    // карточке не висело «not measured yet» до ближайшего таймера.
    Connections {
        target: ConnectionController

        function onConnectionStateChanged() {
            if (!ConnectionController.isConnected) {
                HealthCheckController.startProbe(false)
            }
        }
    }

    // Перепроверка адресов раз в 5 минут (кэш живёт ровно 5 минут, поэтому
    // старше него замеры становятся unknown и проба обновляет их сама).
    Timer {
        id: endpointRecheckTimer
        interval: 5 * 60 * 1000
        repeat: true
        running: !ConnectionController.isConnected
        onTriggered: HealthCheckController.startProbe(false)
    }

    Component.onCompleted: {

        root.openedForIndex = ServersModel.processedIndex
        // чужой отчёт (собранный для другого сервера) показывать и копировать нельзя
        ConnectionReportController.resetReport()

        // Пробы по адресам запускались только из списка серверов и автовыбора —
        // если юзер шёл прямо в карточку, цифр не было вообще («in use»).
        // startProbe(false) уважает кэш: свежие результаты (<5 мин) не перемеряет.
        // Проба ТОЛЬКО если по адресам нет ни одного замера вообще (первая
        // карточка после установки). На каждое открытие заново не меряем:
        // замеры уже идут из списка серверов, при коннекте и после сбоев.
        var probeIps = ServersModel.getProcessedServerData("nodeIps")
        var probeNeeded = false
        for (var pi = 0; pi < probeIps.length; ++pi) {
            var known = ServersModel.processedEndpointHealth(probeIps[pi])
            if (known.ageSec < 0) {
                probeNeeded = true
            }
        }
        // На поднятом туннеле пробы не запускаем: замеры уходят внутрь VPN,
        // и юзер видит «checking…» вместо спокойного экрана.
        if (probeNeeded && !ConnectionController.isConnected) {
            HealthCheckController.startProbe(false)
        }

        // shared connections (frkn://conn) have no subscription account behind
        // them — asking the API for account info errors out, so skip both the
        // refresh and the status row for them
        var apiConfig = ServersModel.getProcessedServerData("apiConfig")
        if (apiConfig && apiConfig.connection_uuid && apiConfig.node_id && apiConfig.shared !== true) {
            root.canShareServer = true
        }
        if (apiConfig && apiConfig.shared === true) {
            root.isSharedConnection = true
            labelsModel = []
            return
        }
        // the card opens with cached data for instant display, but the local copy
        // has no subscription_end_date — refresh from the server so the status
        // row shows "Active · until <date>" (pattern copied from the devices page)
        PageController.showBusyIndicator(true)
        ApiSettingsController.getAccountInfo(true)
        PageController.showBusyIndicator(false)
    }

    function techInfoText() {
        var lines = []
        lines.push(qsTr("Version") + ": " + SettingsController.getAppVersion())
        lines.push(qsTr("Server") + ": " + root.processedServer.name)
        var countryName = ServersModel.getProcessedServerData("countryName")
        var countryCode = ServersModel.getProcessedServerData("countryCode")
        if (countryName !== "" || countryCode !== "") {
            lines.push(qsTr("Connection country") + ": " + (countryName !== "" && countryCode !== ""
                       ? countryName + " (" + countryCode + ")" : countryName + countryCode))
        }
        var protocol = ("" + ServersModel.getProcessedServerData("serviceProtocol")).toUpperCase()
        if (protocol !== "") {
            lines.push(qsTr("Protocol") + ": " + protocol)
        }
        var dns = ApiConfigsController.getCurrentServerDns()
        if (dns !== "") {
            lines.push(qsTr("DNS") + ": " + dns)
        }
        var mtu = SettingsController.mtuOverride > 0
                ? "" + SettingsController.mtuOverride : ApiConfigsController.getCurrentServerMtu()
        if (mtu !== "") {
            lines.push((SettingsController.mtuOverride > 0 ? qsTr("MTU (overridden)") : "MTU") + ": " + mtu)
        }
        var hostName = ServersModel.getProcessedServerData("hostName")
        // «Primary endpoint» ничего не говорил юзеру: показываем адрес, который
        // реально в работе, а если не подключены — тот, с которым пойдём.
        var inUseEndpoint = ""
        var hasInUse = ServersModel.processedIndex === ServersModel.defaultIndex
                && ConnectionController.isConnected && ConnectionController.currentEndpoint !== ""
        if (hasInUse) {
            inUseEndpoint = ConnectionController.currentEndpoint
        } else if (hostName !== "") {
            inUseEndpoint = hostName
        }
        if (inUseEndpoint !== "") {
            lines.push((hasInUse ? qsTr("Endpoint in use") : qsTr("Endpoint")) + ": " + inUseEndpoint)
        }
        var usePort = (hasInUse && ConnectionController.currentPort > 0)
                ? ConnectionController.currentPort : root.processedServerPort
        if (usePort > 0) {
            lines.push((hasInUse ? qsTr("Port in use") : qsTr("Port")) + ": " + usePort)
        }
        var nodeIps = ServersModel.getProcessedServerData("nodeIps")
        if (nodeIps.length > 0) {
            // Copy every endpoint with its own port so support does not have to
            // infer node_ports from the protocol's default port.
            var addressLabels = []
            for (var a = 0; a < nodeIps.length; ++a) {
                var nodePort = ServersModel.getProcessedNodePort(nodeIps[a])
                addressLabels.push(nodePort > 0 ? nodeIps[a] + ":" + nodePort : nodeIps[a])
            }
            lines.push(qsTr("Available addresses") + ": " + addressLabels.join(", "))
        }
        var tunnelIp = ApiConfigsController.getCurrentServerClientIp()
        if (tunnelIp !== "") {
            lines.push(qsTr("Tunnel IP") + ": " + tunnelIp)
        }
        return lines.join("\n")
    }

    Connections {
        target: ServersModel

        function onProcessedServerChanged() {
            root.processedServer = proxyServersModel.get(0)
            root.configRevision++
        }

        // результаты проб приходят асинхронно: без этого биндинги в строках
        // пула считались один раз при создании и цифры не появлялись,
        // пока не переоткроешь карточку
        function onDataChanged() {
            root.configRevision++
        }
    }

    Connections {
        target: ConnectionController

        function onConnectionStateChanged() {
            root.configRevision++
        }
    }

    Connections {
        target: ApiConfigsController

        function onReloadSubscriptionConfigsFinished(success) {
            root.configRevision++
        }
    }

    SortFilterProxyModel {
        id: proxyServersModel
        objectName: "proxyServersModel"

        sourceModel: ServersModel
        filters: [
            ValueFilter {
                roleName: "isCurrentlyProcessed"
                value: true
            }
        ]

        Component.onCompleted: {
            root.processedServer = proxyServersModel.get(0)
        }
    }

    ListViewType {
        id: listView

        anchors.fill: parent

        model: labelsModel

        header: ColumnLayout {
            width: listView.width

            spacing: 4

            BackButtonType {
                id: backButton
                objectName: "backButton"

                Layout.topMargin: 20 + SettingsController.safeAreaTopMargin
            }

            HeaderTypeWithButton {
                id: headerContent
                objectName: "headerContent"

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 10

                actionButtonImage: "qrc:/images/controls/edit-3.svg"

                headerText: root.processedServer.name
                badgeText: root.openedForIndex === ServersModel.defaultIndex ? qsTr("in use") : ""
                descriptionText: root.processedServer.serverDescription !== "" ? root.processedServer.serverDescription : ApiAccountInfoModel.data("serviceDescription")

                actionButtonFunction: function() {
                    serverNameEditDrawer.openTriggered()
                }
            }
        }

        delegate: ColumnLayout {
            width: listView.width
            spacing: 0

            Connections {
                target: ApiAccountInfoModel

                function onModelReset() {
                    delegateItem.rightText = ApiAccountInfoModel.data(contentKey)
                }
            }

            LabelWithImageType {
                id: delegateItem

                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: objectImageSource
                leftText: title
                rightText: ApiAccountInfoModel.data(contentKey)
                rightTextFormat: isRichText ? Text.RichText : Text.PlainText

                visible: rightText !== ""
            }
        }

        footer: ColumnLayout {
            id: footer

            width: listView.width
            spacing: 0

            readonly property bool isVisibleForAmneziaFree: ApiAccountInfoModel.data("isComponentVisible")

            WarningType {
                id: warning

                Layout.topMargin: 32
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.fillWidth: true

                backGroundColor: DopamineStyle.color.translucentRichBrown

                textString: qsTr("Configurations have been updated for some countries. Download and install the updated configuration files")

                iconPath: "qrc:/images/controls/alert-circle.svg"

                visible: {
                    for (let i = 0; i < ApiCountryModel.count; ++i) {
                        if (ApiCountryModel.get(i).isWorkerExpired)
                            return true;
                    }
                    return false;
                }
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/server.svg"
                leftText: SettingsController.dnsOverrideEnabled ? qsTr("DNS (overridden)") : qsTr("DNS")
                rightText: {
                    var overrideEnabled = SettingsController.dnsOverrideEnabled
                    var primary = SettingsController.primaryDns
                    var secondary = SettingsController.secondaryDns
                    return ApiConfigsController.getCurrentServerDns()
                }
                visible: rightText !== ""
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/gauge.svg"
                leftText: SettingsController.mtuOverride > 0 ? qsTr("MTU (overridden)") : qsTr("MTU")
                rightText: SettingsController.mtuOverride > 0
                           ? "" + SettingsController.mtuOverride
                           : ApiConfigsController.getCurrentServerMtu()
                visible: rightText !== ""
            }

            // --- Проверка: два разных действия, взаимоисключающих ---
            // Туннель выключен → проверяем доступность адресов сервера.
            // Туннель включён  → проверяем сам туннель (пробы снаружи бессмысленны).
            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: !ConnectionController.isConnected
                enabled: SettingsController.isTelemetryEnabled && !ConnectionReportController.busy

                text: qsTr("Check available connections")
                descriptionText: {
                    if (!SettingsController.isTelemetryEnabled) {
                        return qsTr("Turn on telemetry in Diagnostics to run the check")
                    }
                    if (ConnectionReportController.busy) {
                        return qsTr("Checking addresses…")
                    }
                    return qsTr("Probes every address of this server: handshake, port, RTT")
                }
                descriptionOnTop: true

                textColor: DopamineStyle.color.richBrown
                rightImageSource: "qrc:/images/controls/refresh-cw.svg"
                rightImageColor: DopamineStyle.color.richBrown

                clickedFunction: function() {
                    if (ConnectionReportController.busy || !SettingsController.isTelemetryEnabled) {
                        return
                    }
                    ConnectionReportController.collect(root.openedForIndex)
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: ConnectionController.isConnected
                enabled: SettingsController.isTelemetryEnabled && !ConnectionReportController.busy

                text: qsTr("Check current connection")
                descriptionText: {
                    if (!SettingsController.isTelemetryEnabled) {
                        return qsTr("Turn on telemetry in Diagnostics to run the check")
                    }
                    if (ConnectionReportController.busy) {
                        return qsTr("Collecting tunnel metrics…")
                    }
                    return qsTr("Tunnel only: DNS through the tunnel, MTU, tunnel IP, session")
                }
                descriptionOnTop: true

                textColor: DopamineStyle.color.richBrown
                rightImageSource: "qrc:/images/controls/refresh-cw.svg"
                rightImageColor: DopamineStyle.color.richBrown

                clickedFunction: function() {
                    if (ConnectionReportController.busy || !SettingsController.isTelemetryEnabled) {
                        return
                    }
                    ConnectionReportController.collect(root.openedForIndex)
                }
            }

            // --- Два действия с уже собранным отчётом: отправить или скопировать ---
            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: SettingsController.isTelemetryEnabled && ConnectionReportController.hasReport
                         && ConnectionReportController.reportIndex === root.openedForIndex
                enabled: ConnectionReportController.sendState !== "sending"

                text: qsTr("Send to support")
                descriptionText: {
                    switch (ConnectionReportController.sendState) {
                    case "sending":
                        return qsTr("Sending…")
                    case "sent":
                        return qsTr("Sent — thanks")
                    case "error":
                        return qsTr("Send failed: %1").arg(ConnectionReportController.sendMessage)
                    default:
                        return qsTr("Send this report to support")
                    }
                }
                descriptionOnTop: true

                rightImageSource: "qrc:/images/controls/mail.svg"
                rightImageColor: DopamineStyle.color.paleGray

                clickedFunction: function() {
                    if (ConnectionReportController.sendState !== "sending") {
                        ConnectionReportController.send()
                    }
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: SettingsController.isTelemetryEnabled && ConnectionReportController.hasReport
                         && ConnectionReportController.reportIndex === root.openedForIndex

                text: qsTr("Copy report")
                descriptionText: qsTr("Same JSON as Send — paste it wherever you need")
                descriptionOnTop: true

                rightImageSource: "qrc:/images/controls/copy.svg"
                rightImageColor: DopamineStyle.color.paleGray

                clickedFunction: function() {
                    GC.copyToClipBoard(ConnectionReportController.reportText)
                    PageController.showNotificationMessage(qsTr("Report copied"))
                }
            }

            // MTU is configured in Settings → Connection; the card only shows
            // whether the value was overridden.

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/server.svg"
                leftText: qsTr("Tunnel IP")
                rightText: {
                    var revision = root.configRevision
                    return ApiConfigsController.getCurrentServerClientIp()
                }
                visible: rightText !== ""
            }

            ListItemTitleType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Technical information")
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/map-pin.svg"
                leftText: qsTr("Connection country")
                rightText: {
                    var countryName = ServersModel.getProcessedServerData("countryName")
                    var countryCode = ServersModel.getProcessedServerData("countryCode")
                    if (countryName !== "" && countryCode !== "") {
                        return countryName + " (" + countryCode + ")"
                    }
                    return countryName !== "" ? countryName : countryCode
                }
                visible: rightText !== ""
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/settings.svg"
                leftText: qsTr("Protocol")
                rightText: ("" + ServersModel.getProcessedServerData("serviceProtocol")).toUpperCase()
                visible: rightText !== ""
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/server.svg"
                // Показываем то, чем реально работаем, а не «первичный» адрес
                // из конфига: подключён — адрес в работе, иначе — адрес по умолчанию.
                property bool showInUse: ServersModel.processedIndex === ServersModel.defaultIndex
                        && ConnectionController.isConnected
                        && ConnectionController.currentEndpoint !== ""
                property string endpointText: showInUse
                        ? ConnectionController.currentEndpoint
                        : ServersModel.getProcessedServerData("hostName")

                leftText: showInUse ? qsTr("Endpoint in use") : qsTr("Endpoint")
                rightText: endpointText
                visible: rightText !== ""
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/radio.svg"

                property bool showInUse: ServersModel.processedIndex === ServersModel.defaultIndex
                        && ConnectionController.isConnected
                        && ConnectionController.currentPort > 0
                property int portValue: showInUse ? ConnectionController.currentPort
                                                  : root.processedServerPort

                leftText: showInUse ? qsTr("Port in use") : qsTr("Port")
                rightText: portValue > 0 ? ("" + portValue) : ""
                visible: rightText !== ""
            }

            // Адреса пула: пользователь может выключить отдельные IP (например,
            // один из них блокирует его оператор). Клиент просто не пробует их.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 0

                visible: ServersModel.getProcessedServerData("nodeIps").length > 1

                CaptionTextType {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8

                    color: DopamineStyle.color.mutedGray

                    // Подпись зависит от состояния: на живом туннеле мерить
                    // нельзя, и это надо сказать прямым текстом, а не показывать
                    // «unreachable». Совет про выключение адресов — только если
                    // в настройках вообще разрешены переключатели адресов.
                    text: {
                        if (ConnectionController.isConnected) {
                            return SettingsController.isEndpointTogglesAllowed
                                    ? qsTr("VPN is on — turn it off to check all addresses. Address changes apply after you reconnect.")
                                    : qsTr("VPN is on — turn it off to check all addresses.")
                        }
                        if (SettingsController.isEndpointTogglesAllowed) {
                            return qsTr("Pool addresses — all of them are used by default. Turn off the ones your operator blocks.")
                        }
                        return qsTr("Pool addresses — all of them are used by default.")
                    }
                }

                Repeater {
                    model: ServersModel.getProcessedServerData("nodeIps")

                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        // Порт — у каждого адреса свой (node_ports), иначе проба
                        // писала результат на другой ключ и адрес навсегда
                        // оставался «not probed yet», хотя замер был
                        readonly property int serverPort: ServersModel.getProcessedNodePort(modelData)
                        // configRevision в зависимостях: иначе значение считается
                        // один раз при создании строки, и результат пробы,
                        // пришедший позже, на экран уже не попадает
                        // последнее ИЗМЕРЕННОЕ значение (свежее или из кэша);
                        // -1 = данных не было вовсе
                        readonly property int lastMs: health.state === "ok" ? health.rtt
                                                          : (health.rttLast >= 0 ? health.rttLast : -1)
                        // ageSec >= 0 — запись о замере есть (пусть и устаревшая);
                        // отрицательный rttLast при hasData = «мерили, ответа не было»
                        readonly property bool hasData: health.ageSec >= 0

                        readonly property var health: {
                            var revision = root.configRevision
                            var healthRevision = ServersModel.endpointHealthRevision
                            var probingNow = HealthCheckController.probing
                            var vpnNow = ConnectionController.isConnected
                            // здоровье адреса — по ТОЙ строке, которая открыта в
                            // карточке: иначе порты берутся у processedIndex и
                            // замер «не находится» (not measured при живом кэше)
                            return ServersModel.endpointHealthFor(root.openedForIndex, modelData)
                        }
                        readonly property bool userDisabled: SettingsController.poolDisabledIps.indexOf(modelData) >= 0
                        // проба сказала «молчит» — адрес не пробуем, и тумблер
                        // показываем выключенным с подписью «no handshake».
                        // В настройки это НЕ пишем: иначе вердикт пробы смешивался
                        // с выбором юзера и позиции переключателей «плыли».
                        readonly property bool probeFailed: !userDisabled
                                                            && health.state === "unreachable"
                        // Визуальное состояние: включён только если юзер не выключал
                        // и проба не сказала «молчит»
                        // Тумблер отражает ТОЛЬКО выбор юзера. Вердикт пробы —
                        // это подпись, а не блокировка: иначе выключить адрес
                        // было нельзя, тап запускал проверку вместо выключения.
                        // Нет рукопожатия (или замера) — галочка выключена.
                        // Это вердикт пробы, а не выбор юзера: в список выключенных
                        // не пишем, при следующем замере галочка вернётся сама.
                        // «Есть замер» — свежий или устаревший: устаревший тоже
                        // живой адрес, галочку из-за него гасить нельзя.
                        readonly property bool hasValue: health.state === "ok" || health.rttLast >= 0
                        // «Нет рукопожатия» — только когда проба реально стучалась
                        // и не получила ответа. Пока идёт проверка или замера
                        // ещё не было, адрес не считаем мёртвым.
                        // «Нет рукопожатия» = замер был и он отрицательный —
                        // свежий (state=unreachable) или из кэша (rtt -1).
                        // Тумблер в этом состоянии выключен и неактивен: клиент
                        // всё равно не пойдёт в этот адрес.
                        readonly property bool noHandshake: !userDisabled
                                                           && hasData
                                                           && lastMs < 0
                        readonly property bool addressEnabled: !userDisabled && !noHandshake

                        readonly property bool inUse: ConnectionController.isConnected
                                                      && ConnectionController.currentEndpoint === modelData

                        // Цветной индикатор вместо цифр, когда юзер выключил
                        // «показывать пинг»: perfect / good / bad / недоступен.
                        readonly property string dotText: {
                            if (userDisabled) {
                                return qsTr("off")
                            }
                            if (HealthCheckController.probing && health.state !== "unreachable") {
                                if (lastMs < 0) {
                                    return "<font color=\"#8A8A8A\">" + qsTr("checking… · —") + "</font>"
                                }
                                var tone = lastMs < 100 ? "#3FBF6F" : (lastMs < 120 ? "#E0B341" : "#D9534F")
                                return "<font color=\"#8A8A8A\">" + qsTr("checking… · ") + "</font>"
                                     + "<font color=\"" + tone + "\">● " + lastMs + " ms last</font>"
                            }
                            if (health.state === "unreachable") {
                                return "<font color=\"#D9534F\">✕</font>"
                            }
                            var ms = health.state === "ok" ? health.rtt
                                                          : (health.rttLast >= 0 ? health.rttLast : -1)
                            if (ms < 0 && hasData) {
                                return "<font color=\"#D9534F\">✕</font>"
                            }
                            if (ms < 0) {
                                // не «мёртвый», а «не мерили»: на туннеле пробы
                                // запрещены, иначе просто ещё не дошли руки
                                if (ConnectionController.isConnected) {
                                    return "<font color=\"#8A8A8A\">" + qsTr("not checked — VPN is on") + "</font>"
                                }
                                return "<font color=\"#8A8A8A\">" + qsTr("not measured yet") + "</font>"
                            }
                            // perfect < 100 мс, good 100–119, bad ≥ 120
                            var color = ms < 100 ? "#3FBF6F" : (ms < 120 ? "#E0B341" : "#D9534F")
                            var suffix = inUse ? qsTr(" · in use") : ""
                            return "<font color=\"" + color + "\">●</font>" + suffix
                        }

                        readonly property string statusText: {
                            if (userDisabled) {
                                return qsTr("skipped — turned off by you")
                            }
                            if (HealthCheckController.probing) {
                                return lastMs >= 0
                                        ? qsTr("checking… · %1 ms last").arg(lastMs)
                                        : qsTr("checking… · —")
                            }
                            if (probeFailed) {
                                return qsTr("unreachable")
                            }
                            // Цифры показываем всегда, когда замер есть: карточка —
                            // диагностическая поверхность, и гейт «Show server ping
                            // values» (по умолчанию выключен) оставлял здесь «in use».
                            if (health.state === "ok" && health.rtt >= 0) {
                                // «in use» — только тот адрес, к которому клиент
                                // подключён прямо сейчас; остальные живые — available
                                return inUse ? health.rtt + qsTr(" ms · in use")
                                             : health.rtt + qsTr(" ms · available")
                            }
                            // устаревший замер тоже показываем: цифра с пометкой
                            // полезнее прочерка
                            if (health.rttLast >= 0) {
                                return inUse ? health.rttLast + qsTr(" ms · cached · in use")
                                             : health.rttLast + qsTr(" ms · cached · available")
                            }
                            // замер был, но проба не получила ответа — это
                            // «unreachable», а не «не мерили»
                            if (hasData) {
                                return inUse ? qsTr("unreachable · cached · in use")
                                             : qsTr("unreachable · cached")
                            }
                            // «checking…» — только когда проба реально имеет смысл
                            // (туннель выключен); на подключённом не показываем
                            if (HealthCheckController.probing && !ConnectionController.isConnected) {
                                return qsTr("checking…")
                            }
                            // данных нет: на подключённом туннеле это НЕ «недоступно» —
                            // мы просто не мерили (пробы через VPN запрещены)
                            // Замера не было. «unreachable» здесь — враньё: на живом
                            // туннеле пробы запрещены, а без туннеля мы просто ещё
                            // не мерили. Пишем честно, что именно происходит.
                            if (ConnectionController.isConnected) {
                                return inUse ? qsTr("in use") : qsTr("not checked — VPN is on")
                            }
                            return inUse ? qsTr("in use") : qsTr("not measured yet")
                        }

                        SwitcherType {
                            id: endpointSwitch

                            Layout.fillWidth: true

                            // структура одна и та же; при выключенной настройке
                            // прячем только сам тумблер
                            showIndicator: SettingsController.isEndpointTogglesAllowed

                            text: modelData
                            // пинги выключены в настройках — показываем точки, но
                            // структура строки та же (версия не «прыгает»)
                            descriptionText: SettingsController.isServerPingTextVisible ? statusText : dotText

                            opacity: userDisabled ? 0.45 : 1.0
                            Behavior on opacity { NumberAnimation { duration: 120 } }

                            // Тумблер обрабатывает тап сам (Switch внутри), наружу
                            // отдаём только запись в настройки. checked ниже
                            // восстанавливается биндингом после записи.
                            checked: addressEnabled
                            // выключено вердиктом пробы — руками не переключить;
                            // при выключенной настройке тумблера нет вовсе
                            enabled: SettingsController.isEndpointTogglesAllowed && !noHandshake

                            onToggled: function() {

                                // Цель — противоположность сохранённому состоянию:
                                // сейчас включён (userDisabled=false) → выключаем.
                                var wantEnabled = userDisabled

                                // Список ведём в SettingsController.poolDisabledIps:
                                // Q_PROPERTY с NOTIFY сам обновит все строки.
                                var list = SettingsController.poolDisabledIps.slice()
                                var pos = list.indexOf(modelData)
                                if (wantEnabled) {
                                    if (pos >= 0) {
                                        list.splice(pos, 1)
                                    }
                                } else if (pos < 0) {
                                    list.push(modelData)
                                }
                                SettingsController.poolDisabledIps = list

                                // На поднятом туннеле набор адресов уже в конфиге:
                                // изменение применится только после переподключения.
                                if (ConnectionController.isConnected) {
                                    PageController.showNotificationMessage(
                                        qsTr("Saved — applies after you reconnect the VPN"))
                                }

                                // Switch ломает биндинг своим присваиванием —
                                // возвращаем, чтобы визуал всегда шёл от настроек
                                checked = Qt.binding(function() { return addressEnabled })
                            }
                        }

                    }
                }
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/gauge.svg"
                leftText: qsTr("Speed")
                rightText: "↓ " + ConnectionController.downloadSpeed + "   ↑ " + ConnectionController.uploadSpeed
                visible: ServersModel.processedIndex === ServersModel.defaultIndex
                         && ConnectionController.isConnected && ConnectionController.downloadSpeed !== ""
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/gauge.svg"
                leftText: qsTr("Ping")
                rightText: ConnectionController.ping + " ms"
                visible: ServersModel.processedIndex === ServersModel.defaultIndex
                         && ConnectionController.isConnected
                         && ConnectionController.ping !== ""
                         && SettingsController.isServerPingTextVisible
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                text: qsTr("Copy technical information")
                rightImageSource: "qrc:/images/controls/copy.svg"

                clickedFunction: function() {
                    GC.copyToClipBoard(root.techInfoText())
                    PageController.showNotificationMessage(qsTr("Copied"))
                }
            }

            DividerType {}

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 32

                text: qsTr("Configuration Files")

                descriptionText: qsTr("WireGuard configuration file (INI) for routers and other clients")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                visible: ApiConfigsController.getCurrentServerConfigIni() !== ""

                clickedFunction: function() {
                    configPopup.text = ApiConfigsController.getCurrentServerConfigIni()
                    configPopup.titleText = qsTr("Configuration file (INI)")
                    configPopup.open()
                }
            }

            DividerType {
                visible: footer.isVisibleForAmneziaFree
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: root.canShareServer

                text: qsTr("Share connection")

                descriptionText: qsTr("A link for this server only, not the whole subscription")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsShare)
                }
            }

            DividerType {
                visible: root.canShareServer
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: footer.isVisibleForAmneziaFree ? 0 : 32

                text: qsTr("Support")
                descriptionText: qsTr("Write to us, email, or Telegram")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsApiSupport)
                }
            }

            DividerType {}

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: footer.isVisibleForAmneziaFree

                text: qsTr("How to connect on another device")
                descriptionText: "frkn.org/setup"
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    Qt.openUrlExternally("https://frkn.org/setup")
                }
            }

            DividerType {
                visible: footer.isVisibleForAmneziaFree
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                text: qsTr("Show raw config")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    configPopup.text = ApiConfigsController.getCurrentServerConfigJson()
                    configPopup.titleText = qsTr("Raw JSON")
                    configPopup.open()
                }
            }

            DividerType {}

            Popup {
                id: configPopup
                parent: Overlay.overlay
                width: parent.width - 50
                height: parent.height * 0.7
                modal: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                property alias text: configTextArea.text
                property string titleText: ""

                background: Rectangle {
                    anchors.fill: parent
                    color: DopamineStyle.color.charcoalGray
                    radius: 4
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    BaseHeaderType {
                        id: popupHeader
                        visible: configPopup.titleText !== ""
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.topMargin: 16
                        headerText: configPopup.titleText
                    }

                    TextAreaType {
                        id: configTextArea
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: 16
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: 16
                        Layout.bottomMargin: 16
                        spacing: 8

                        BasicButtonType {
                            id: copyButton

                            implicitHeight: 32

                            defaultColor: DopamineStyle.color.richBrown
                            hoveredColor: DopamineStyle.color.lightBrown
                            pressedColor: DopamineStyle.color.lightBrown
                            disabledColor: DopamineStyle.color.charcoalGray

                            textColor: DopamineStyle.color.paleGray
                            borderWidth: 0

                            text: qsTr("Copy")

                            clickedFunc: function() {
                                GC.copyToClipBoard(configTextArea.text)
                                PageController.showNotificationMessage(qsTr("Copied"))
                            }
                        }

                        BasicButtonType {
                            id: closeButton

                            implicitHeight: 32

                            defaultColor: DopamineStyle.color.mutedGray
                            hoveredColor: DopamineStyle.color.lightGray
                            pressedColor: DopamineStyle.color.lightGray
                            disabledColor: DopamineStyle.color.charcoalGray

                            textColor: DopamineStyle.color.midnightBlack
                            borderWidth: 0

                            text: qsTr("Close")

                            clickedFunc: function() {
                                configPopup.close()
                            }
                        }
                    }
                }
            }

            BasicButtonType {
                id: resetButton
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 24
                Layout.bottomMargin: 16
                Layout.leftMargin: 8
                implicitHeight: 32

                defaultColor: "transparent"
                hoveredColor: DopamineStyle.color.translucentWhite
                pressedColor: DopamineStyle.color.sheerWhite
                textColor: DopamineStyle.color.vibrantRed

                text: qsTr("Reload API config")

                clickedFunc: function() {
                    var headerText = qsTr("Reload API config?")
                    var yesButtonText = qsTr("Continue")
                    var noButtonText = qsTr("Cancel")

                    var yesButtonFunction = function() {
                        if (ServersModel.isDefaultServerCurrentlyProcessed() && ConnectionController.isConnected) {
                            PageController.showNotificationMessage(qsTr("Cannot reload API config during active connection"))
                        } else {
                            PageController.showBusyIndicator(true)
                            ApiConfigsController.updateServiceFromGateway(ServersModel.processedIndex, "", "", true)
                            PageController.showBusyIndicator(false)
                        }
                    }
                    var noButtonFunction = function() {
                    }

                    showQuestionDrawer(headerText, "", yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
                }
            }
        }
    }

    RenameServerDrawer {
        id: serverNameEditDrawer

        anchors.fill: parent
        expandedHeight: parent.height * 0.35

        serverNameText: root.processedServer.name
    }
}
