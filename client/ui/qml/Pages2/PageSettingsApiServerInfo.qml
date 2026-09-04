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

    Component.onCompleted: {
        // shared connections (frkn://conn) have no subscription account behind
        // them — asking the API for account info errors out, so skip both the
        // refresh and the status row for them
        var apiConfig = ServersModel.getProcessedServerData("apiConfig")
        if (apiConfig && apiConfig.shared === true) {
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
            lines.push(qsTr("Country") + ": " + (countryName !== "" && countryCode !== ""
                       ? countryName + " (" + countryCode + ")" : countryName + countryCode))
        }
        var protocol = ("" + ServersModel.getProcessedServerData("serviceProtocol")).toUpperCase()
        if (protocol !== "") {
            lines.push(qsTr("Protocol") + ": " + protocol)
        }
        var hostName = ServersModel.getProcessedServerData("hostName")
        if (hostName !== "") {
            lines.push(qsTr("Primary endpoint") + ": " + hostName)
        }
        var nodeIps = ServersModel.getProcessedServerData("nodeIps")
        if (nodeIps.length > 1) {
            lines.push(qsTr("Available addresses") + ": " + nodeIps.join(", "))
        }
        if (ServersModel.processedIndex === ServersModel.defaultIndex
                && ConnectionController.isConnected && ConnectionController.currentEndpoint !== "") {
            lines.push(qsTr("Endpoint in use") + ": " + ConnectionController.currentEndpoint)
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

                backGroundColor: AmneziaStyle.color.translucentRichBrown

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
                leftText: qsTr("DNS")
                rightText: ApiConfigsController.getCurrentServerDns()
                visible: rightText !== ""
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/settings.svg"
                leftText: qsTr("MTU")
                rightText: ApiConfigsController.getCurrentServerMtu()
                visible: rightText !== ""
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/server.svg"
                leftText: qsTr("Tunnel IP")
                rightText: ApiConfigsController.getCurrentServerClientIp()
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
                leftText: qsTr("Country")
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
                leftText: qsTr("Primary endpoint")
                rightText: ServersModel.getProcessedServerData("hostName")
                visible: rightText !== ""
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/server.svg"
                leftText: qsTr("Available addresses")
                rightText: ServersModel.getProcessedServerData("nodeIps").join(", ")
                visible: ServersModel.getProcessedServerData("nodeIps").length > 1
            }

            LabelWithImageType {
                Layout.fillWidth: true
                Layout.margins: 16

                imageSource: "qrc:/images/controls/info.svg"
                leftText: qsTr("Endpoint in use")
                rightText: ConnectionController.currentEndpoint
                visible: ServersModel.processedIndex === ServersModel.defaultIndex
                         && ConnectionController.isConnected && rightText !== ""
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

                visible: footer.isVisibleForAmneziaFree

                text: qsTr("Active Devices")

                descriptionText: qsTr("Manage currently connected devices")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    ApiSettingsController.updateApiDevicesModel()
                    PageController.goToPage(PageEnum.PageSettingsApiDevices)
                }
            }

            DividerType {
                visible: footer.isVisibleForAmneziaFree
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: footer.isVisibleForAmneziaFree ? 0 : 32

                text: qsTr("Support")
                descriptionText: "frkn.org/support"
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    Qt.openUrlExternally("https://frkn.org/support")
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
                    color: AmneziaStyle.color.charcoalGray
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

                            defaultColor: AmneziaStyle.color.richBrown
                            hoveredColor: AmneziaStyle.color.lightBrown
                            pressedColor: AmneziaStyle.color.lightBrown
                            disabledColor: AmneziaStyle.color.charcoalGray

                            textColor: AmneziaStyle.color.paleGray
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

                            defaultColor: AmneziaStyle.color.mutedGray
                            hoveredColor: AmneziaStyle.color.lightGray
                            pressedColor: AmneziaStyle.color.lightGray
                            disabledColor: AmneziaStyle.color.charcoalGray

                            textColor: AmneziaStyle.color.midnightBlack
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
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                textColor: AmneziaStyle.color.vibrantRed

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

            BasicButtonType {
                id: revokeButton
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 16
                Layout.leftMargin: 8
                implicitHeight: 32

                visible: footer.isVisibleForAmneziaFree

                defaultColor: "transparent"
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                textColor: AmneziaStyle.color.vibrantRed

                text: qsTr("Unlink this device")

                clickedFunc: function() {
                    var headerText = qsTr("Are you sure you want to unlink this device?")
                    var descriptionText = qsTr("This will unlink the device from your subscription. You can reconnect it anytime by pressing \"Reload API config\" in subscription settings on device.")
                    var yesButtonText = qsTr("Continue")
                    var noButtonText = qsTr("Cancel")

                    var yesButtonFunction = function() {
                        if (ServersModel.isDefaultServerCurrentlyProcessed() && ConnectionController.isConnected) {
                            PageController.showNotificationMessage(qsTr("Cannot unlink device during active connection"))
                        } else {
                            PageController.showBusyIndicator(true)
                            if (ApiConfigsController.deactivateDevice(false)) {
                                ApiSettingsController.getAccountInfo(true, true)
                            }
                            PageController.showBusyIndicator(false)
                        }
                    }
                    var noButtonFunction = function() {
                    }

                    showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
                }
            }

            BasicButtonType {
                id: removeButton
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 16
                Layout.leftMargin: 8
                implicitHeight: 32

                defaultColor: "transparent"
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                textColor: AmneziaStyle.color.vibrantRed

                text: qsTr("Remove from application")

                clickedFunc: function() {
                    var headerText = qsTr("Remove from application?")
                    var yesButtonText = qsTr("Continue")
                    var noButtonText = qsTr("Cancel")

                    var yesButtonFunction = function() {
                        if (ServersModel.isDefaultServerCurrentlyProcessed() && ConnectionController.isConnected) {
                            PageController.showNotificationMessage(qsTr("Cannot remove server during active connection"))
                        } else {
                            PageController.showBusyIndicator(true)
                            InstallController.removeProcessedServer()
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
