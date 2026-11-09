import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QtCore

import PageEnum 1.0
import Style 1.0

import "../Controls2"
import "../Config"
import "../Components"
import "../Controls2/TextTypes"

PageType {
    id: root

    // Скролл сбрасываем при открытии страницы, а не по фокусу кнопки «назад»:
    // фокус переезжает во время обычных свайпов, и список возвращало наверх.
    onVisibleChanged: {
        if (visible) {
            listView.positionViewAtBeginning()
        }
    }

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin

    }

    ListViewType {
        id: listView

        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.left: parent.left

        header: ColumnLayout {
            width: listView.width

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Diagnostics")
                descriptionText: qsTr("Logs are stored only on this device. Enable log saving in case of application malfunction. " +
                                      "By default, logging functionality is disabled.")
            }

            SwitcherType {
                id: switcher

                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Enable logs")

                checked: SettingsController.isLoggingEnabled
                
                onToggled: function() {
                    if (checked !== SettingsController.isLoggingEnabled) {
                        SettingsController.isLoggingEnabled = checked
                    }
                }
            }

            DividerType {}

            SwitcherType {
                id: telemetrySwitcher

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Share network telemetry")
                descriptionText: qsTr("Probes how this device reaches our servers (DNS, ping, ports) and lets you send " +
                                      "the result to support. Collected on this device only, sent only when you tap " +
                                      "\"Send to support\". Off by default.")

                checked: SettingsController.isTelemetryEnabled

                onToggled: function() {
                    if (checked !== SettingsController.isTelemetryEnabled) {
                        SettingsController.isTelemetryEnabled = checked
                    }
                }
            }

            DividerType {}

            SwitcherType {
                id: pingTextSwitcher

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Show server ping values")
                descriptionText: qsTr("By default the server list shows only a colored status dot. Enable to see the exact latency in milliseconds.")

                checked: SettingsController.isServerPingTextVisible

                onToggled: function() {
                    if (checked !== SettingsController.isServerPingTextVisible) {
                        SettingsController.isServerPingTextVisible = checked
                    }
                }
            }

            DividerType {}

            LabelWithButtonType {
                id: connectionScan
                Layout.fillWidth: true

                visible: SettingsController.isTelemetryEnabled

                text: ConnectionReportController.hasScan && !ConnectionReportController.scanning
                      ? qsTr("Check all servers again")
                      : qsTr("Check all servers")
                descriptionText: {
                    if (ConnectionController.isConnected) {
                        return qsTr("VPN is on — turn off the VPN to check all servers. Through a tunnel the probe measures the tunnel, not the servers.")
                    }
                    if (ConnectionReportController.scanning) {
                        return qsTr("Probing %1 of %2… tap to cancel")
                                 .arg(ConnectionReportController.progressDone)
                                 .arg(ConnectionReportController.progressTotal)
                    }
                    if (ConnectionReportController.hasScan) {
                        return qsTr("Scan ready — send it below or copy to clipboard")
                    }
                    return qsTr("Probe every address of every server in your subscription")
                }
                descriptionOnTop: true

                enabled: !ConnectionController.isConnected || ConnectionReportController.scanning

                rightImageSource: ConnectionReportController.scanning
                                 ? "qrc:/images/controls/close.svg"
                                 : "qrc:/images/controls/scan-line.svg"
                rightImageColor: DopamineStyle.color.paleGray

                clickedFunction: function() {
                    if (ConnectionReportController.busy && !ConnectionReportController.scanning) {
                        return
                    }
                    if (ConnectionReportController.scanning) {
                        ConnectionReportController.cancel()
                        return
                    }
                    ConnectionReportController.collectAll()
                }
            }
            DividerType {}

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: -8

                visible: ConnectionReportController.hasScan && !ConnectionReportController.scanning

                text: qsTr("Send scan to support")
                descriptionText: {
                    if (ConnectionReportController.sendState === "sending") {
                        return qsTr("Sending…")
                    }
                    if (ConnectionReportController.sendMessage !== "") {
                        return ConnectionReportController.sendMessage
                    }
                    return qsTr("Uploads this scan with your account tag")
                }
                descriptionOnTop: true

                rightImageSource: "qrc:/images/controls/share-2.svg"
                rightImageColor: DopamineStyle.color.paleGray

                clickedFunction: function() {
                    ConnectionReportController.send()
                }
            }

            DividerType {
                visible: ConnectionReportController.hasScan && !ConnectionReportController.scanning
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: -8

                visible: ConnectionReportController.hasScan && !ConnectionReportController.scanning

                text: qsTr("Copy scan to clipboard")
                descriptionText: qsTr("Same text as the upload — paste it in the chat if sending fails")
                descriptionOnTop: true

                rightImageSource: "qrc:/images/controls/copy.svg"
                rightImageColor: DopamineStyle.color.paleGray

                clickedFunction: function() {
                    ConnectionReportController.copyReport()
                    PageController.showNotificationMessage(qsTr("Scan copied to clipboard"))
                }
            }

            Connections {
                target: ConnectionReportController

                function onReportChanged() {
                    // Отчёт уходит на сервер автоматически (см. finishReport/finishScan),
                    // в буфер его юзеру не кладём — это никому не нужно.
                }
            }

            // Когда клиент последний раз сам отправил отчёт (провал подключения).
            LabelWithImageType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: SettingsController.isTelemetryEnabled

                imageSource: "qrc:/images/controls/settings.svg"
                leftText: qsTr("Last auto report")
                rightText: {
                    var iso = ConnectionReportController.lastAutoReportAt
                    if (!iso) {
                        return qsTr("never")
                    }
                    var date = new Date(iso)
                    return isNaN(date.getTime()) ? iso : date.toLocaleString(Qt.locale(), Locale.ShortFormat)
                }
            }

            DividerType {}

            DividerType {}

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: -8

                text: qsTr("Clear logs")
                leftImageSource: "qrc:/images/controls/trash.svg"
                isSmallLeftImage: true

                clickedFunction: function() {
                    var headerText = qsTr("Clear logs?")
                    var yesButtonText = qsTr("Continue")
                    var noButtonText = qsTr("Cancel")

                    var yesButtonFunction = function() {
                        PageController.showBusyIndicator(true)
                        SettingsController.clearLogs()
                        PageController.showBusyIndicator(false)
                        PageController.showNotificationMessage(qsTr("Logs have been cleaned up"))
                    }

                    var noButtonFunction = function() {

                    }

                    showQuestionDrawer(headerText, "", yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
                }
            }
        }

        model: logTypes

        // SnapOneItem здесь и давал «залипание»: вся страница — один высокий
        // делегат, и список постоянно притягивал его к началу.
        snapMode: ListView.NoSnap
        boundsBehavior: Flickable.StopAtBounds
        // 4000px kept dozens of delegates alive far outside the viewport.
        cacheBuffer: 800

        delegate: ColumnLayout {
            id: delegateContent

            width: listView.width
            // ColumnLayout сам по себе не отдаёт высоту списку: без этого
            // contentHeight оставался меньше содержимого, и скролл отскакивал
            // к началу, пока список не пересчитает раскладку.
            height: implicitHeight

            enabled: isVisible

            ListItemTitleType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: title
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                color: DopamineStyle.color.mutedGray

                text: description
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: -8
                Layout.bottomMargin: -8

                visible: !GC.isMobile()

                text: qsTr("Open logs folder")
                leftImageSource: "qrc:/images/controls/folder-open.svg"
                isSmallLeftImage: true

                clickedFunction: openLogsHandler
            }

            DividerType {}

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: -8
                Layout.bottomMargin: -8

                text: qsTr("Export logs")
                leftImageSource: "qrc:/images/controls/save.svg"
                isSmallLeftImage: true

                clickedFunction: exportLogsHandler
            }

            DividerType {}
        }
    }

    // Show service logs only if this is NOT a macOS build with
    // Network-Extension (IsMacOsNeBuild is injected from C++ at run-time)
    // or if this is NOT a mobile build
    property list<QtObject> logTypes: (IsMacOsNeBuild || GC.isMobile()) ? [
        clientLogs
    ] : [
        clientLogs,
        serviceLogs
    ]

    QtObject {
        id: clientLogs

        readonly property string title: qsTr("Client logs")
        readonly property string description: qsTr("Dopamine logs")
        readonly property bool isVisible: true
        readonly property var openLogsHandler: function() {
            SettingsController.openLogsFolder()
        }
        readonly property var exportLogsHandler: function() {
            var fileName = ""
            if (GC.isMobile()) {
                fileName = "Dopamine.log"
            } else {
                fileName = SystemController.getFileName(qsTr("Save"),
                                                        qsTr("Logs files (*.log)"),
                                                        StandardPaths.standardLocations(StandardPaths.DocumentsLocation) + "/Dopamine",
                                                        true,
                                                        ".log")
            }
            if (fileName !== "") {
                PageController.showBusyIndicator(true)
                SettingsController.exportLogsFile(fileName)
                PageController.showBusyIndicator(false)
                PageController.showNotificationMessage(qsTr("Logs file saved"))
            }
        }
    }

    QtObject {
        id: serviceLogs

        readonly property string title: qsTr("Service logs")
        readonly property string description: qsTr("Dopamine-service logs")
        readonly property bool isVisible: !GC.isMobile() && !IsMacOsNeBuild
        readonly property var openLogsHandler: function() {
            SettingsController.openServiceLogsFolder()
        }
        readonly property var exportLogsHandler: function() {
            var fileName = ""
            fileName = SystemController.getFileName(qsTr("Save"),
                                                    qsTr("Logs files (*.log)"),
                                                    StandardPaths.standardLocations(StandardPaths.DocumentsLocation) + "/dopamine-service",
                                                    true,
                                                    ".log")
            if (fileName !== "") {
                PageController.showBusyIndicator(true)
                SettingsController.exportServiceLogsFile(fileName)
                PageController.showBusyIndicator(false)
                PageController.showNotificationMessage(qsTr("Logs file saved"))
            }
        }
    }
}
