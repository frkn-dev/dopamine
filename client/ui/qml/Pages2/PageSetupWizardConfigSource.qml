import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import QtCore

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

PageType {
    id: root

    Component.onCompleted: {
        if (ImportController.hasPendingSubscription) {
            subscriptionDrawer.configCount = ImportController.subscriptionConfigsCount()
            subscriptionDrawer.openTriggered()
        }
    }

    Connections {
        target: ImportController

        function onQrDecodingFinished() {
            ImportController.queueConfigForConfirmation()
        }

        function onSubscriptionConfigsReady(count) {
            PageController.showBusyIndicator(false)
            subscriptionDrawer.configCount = count
            subscriptionDrawer.openTriggered()
        }

        function onSubscriptionErrorOccurred(message) {
            PageController.showBusyIndicator(false)
            PageController.showErrorMessage(message)
        }

        function onSubscriptionAllDuplicates() {
            PageController.showBusyIndicator(false)
            PageController.showNotificationMessage(qsTr("All configurations have already been added"))
        }

        function onImportFinished() {
            PageController.showBusyIndicator(false)
            if (!ConnectionController.isConnected) {
                ServersModel.setDefaultServerIndex(ServersModel.getServersCount() - 1)
                ServersModel.processedIndex = ServersModel.defaultIndex
            }
            PageController.goToPageHome()
        }
    }

    ListViewType {
        id: listView

        anchors.fill: parent

        header: ColumnLayout {
            width: listView.width

            HeaderTypeWithButton {
                id: moreButton

                property bool isVisible: SettingsController.getInstallationUuid() !== "" || PageController.isStartPageVisible()

                Layout.fillWidth: true
                Layout.topMargin: 24 + SettingsController.safeAreaTopMargin
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: qsTr("Connection")

                actionButtonImage: isVisible ? "qrc:/images/controls/more-vertical.svg" : ""
                actionButtonFunction: function() {
                    moreActionsDrawer.openTriggered()
                }

                DrawerType2 {
                    id: moreActionsDrawer

                    parent: root

                    anchors.fill: parent
                    expandedHeight: root.height * 0.5

                    expandedStateContent: ColumnLayout {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 0

                        BaseHeaderType {
                            Layout.fillWidth: true
                            Layout.topMargin: 32
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16

                            headerText: qsTr("Settings")
                        }

                        SwitcherType {
                            id: switcher
                            Layout.fillWidth: true
                            Layout.topMargin: 16
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16

                            text: qsTr("Enable logs")

                            visible: PageController.isStartPageVisible()
                            checked: SettingsController.isLoggingEnabled
                            onToggled: function() {
                                if (checked !== SettingsController.isLoggingEnabled) {
                                    SettingsController.isLoggingEnabled = checked
                                }
                            }
                        }

                        LabelWithButtonType {
                            Layout.fillWidth: true

                            text: qsTr("Export client logs")
                            rightImageSource: "qrc:/images/controls/chevron-right.svg"

                            visible: PageController.isStartPageVisible()

                            clickedFunction: function() {
                                var fileName = ""
                                if (GC.isMobile()) {
                                    fileName = "FRKN.log"
                                } else {
                                    fileName = SystemController.getFileName(qsTr("Save"),
                                                                            qsTr("Logs files (*.log)"),
                                                                            StandardPaths.standardLocations(StandardPaths.DocumentsLocation) + "/FRKN",
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

                        LabelWithButtonType {
                            id: supportUuid
                            Layout.fillWidth: true
                            Layout.topMargin: 16

                            text: qsTr("Support tag")
                            descriptionText: SettingsController.getInstallationUuid()

                            descriptionOnTop: true

                            rightImageSource: "qrc:/images/controls/copy.svg"
                            rightImageColor: AmneziaStyle.color.paleGray

                            visible: SettingsController.getInstallationUuid() !== ""
                            clickedFunction: function() {
                                GC.copyToClipBoard(descriptionText)
                                PageController.showNotificationMessage(qsTr("Copied"))
                                if (!GC.isMobile()) {
                                    this.rightButton.forceActiveFocus()
                                }
                            }
                        }
                    }
                }
            }

            ParagraphTextType {
                objectName: "insertKeyLabel"

                Layout.fillWidth: true
                Layout.topMargin: 32
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 24

                text: qsTr("Insert the key, add a configuration file or scan the QR-code")
            }

            TextFieldWithHeaderType {
                id: textKey

                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: qsTr("Insert key")
                buttonText: qsTr("Insert")

                clickedFunc: function() {
                    textField.text = ""
                    textField.paste()
                }
            }

            BasicButtonType {
                id: continueButton

                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                visible: textKey.textField.text !== ""

                text: qsTr("Continue")

                clickedFunc: function() {
                    var inputText = textKey.textField.text.trim()
                    if (ImportController.extractConfigFromData(inputText)) {
                        ImportController.queueConfigForConfirmation()
                    } else {
                        // If extractConfigFromData returned false, it might be an async
                        // subscription fetch in progress — show busy indicator.
                        // The indicator will be hidden by signal handlers above.
                        var isFrkn = inputText.startsWith("frkn://")
                        var isFrknSubscription = inputText.startsWith("frkn://sub/")
                        var isUuid = /^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$/.test(inputText)
                        var isHttp = inputText.startsWith("http://") || inputText.startsWith("https://")
                        var isPlainId = !inputText.includes(" ") && !inputText.includes("://")
                        if ((isFrkn && !isFrknSubscription) || isHttp || (isPlainId && !isUuid)) {
                            PageController.showBusyIndicator(true)
                        }
                    }
                }
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.topMargin: 32
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 24

                color: AmneziaStyle.color.charcoalGray
                text: qsTr("Other connection options")
            }
        }

        model: variants

        delegate: ColumnLayout {
            width: listView.width

            CardWithIconsType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 16

                visible: isVisible

                headerText: title
                bodyText: description

                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                leftImageSource: imageSource

                onClicked: { handler() }

                Keys.onEnterPressed: this.clicked()
                Keys.onReturnPressed: this.clicked()
            }
        }

        footer: ColumnLayout {
            width: listView.width

            visible: false

            BasicButtonType {
                id: siteLink2
                Layout.topMargin: 24
                Layout.bottomMargin: 16
                Layout.alignment: Qt.AlignHCenter
                implicitHeight: 32

                visible: Qt.platform.os !== "ios" && !IsMacOsNeBuild

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                disabledColor: AmneziaStyle.color.mutedGray
                textColor: AmneziaStyle.color.goldenApricot

                text: qsTr("FRKN website")

                rightImageSource: "qrc:/images/controls/external-link.svg"

                clickedFunc: function() {
                    Qt.openUrlExternally(LanguageModel.getCurrentSiteUrl())
                }
            }
        }
    }

    property list<QtObject> variants: [
        qrScan,
        siteLink
    ]

    QtObject {
        id: amneziaVpn

        property string title: qsTr("VPN by FRKN")
        property string description: qsTr("Connect to classic paid and free VPN services from FRKN")
        property string imageSource: "qrc:/images/controls/amnezia.svg"
        property bool isVisible: true
        property var handler: function() {
            PageController.showBusyIndicator(true)
            var result = ApiConfigsController.fillAvailableServices()
            PageController.showBusyIndicator(false)
            if (result) {
                PageController.goToPage(PageEnum.PageSetupWizardApiServicesList)
            }
        }
    }

    QtObject {
        id: selfHostVpn

        property string title: qsTr("Self-hosted VPN")
        property string description: qsTr("Configure Dopamine VPN on your own server")
        property string imageSource: "qrc:/images/controls/server.svg"
        property bool isVisible: true
        property var handler: function() {
            PageController.goToPage(PageEnum.PageSetupWizardCredentials)
        }
    }

    QtObject {
        id: backupRestore

        property string title: qsTr("Restore from backup")
        property string description: qsTr("")
        property string imageSource: "qrc:/images/controls/archive-restore.svg"
        property bool isVisible: PageController.isStartPageVisible()
        property var handler: function() {
            var filePath = SystemController.getFileName(qsTr("Open backup file"),
                                                        qsTr("Backup files (*.backup)"))
            if (filePath !== "") {
                PageController.showBusyIndicator(true)
                SettingsController.restoreAppConfig(filePath)
                PageController.showBusyIndicator(false)
            }
        }
    }

    QtObject {
        id: fileOpen

        property string title: qsTr("File with connection settings")
        property string description: qsTr("")
        property string imageSource: "qrc:/images/controls/folder-search-2.svg"
        property bool isVisible: true
        property var handler: function() {
            var nameFilter = !ServersModel.getServersCount() ? "Config or backup files (*.vpn *.ovpn *.conf *.json *.backup)" :
                                                               "Config files (*.vpn *.ovpn *.conf *.json)"
            var fileName = SystemController.getFileName(qsTr("Open config file"), nameFilter)
            if (fileName !== "") {
                if (ImportController.extractConfigFromFile(fileName)) {
                    ImportController.queueConfigForConfirmation()
                }
            }
        }
    }

    QtObject {
        id: qrScan

        property string title: qsTr("QR code")
        property string description: qsTr("")
        property string imageSource: "qrc:/images/controls/scan-line.svg"
        property bool isVisible: SettingsController.isCameraPresent()
        property var handler: function() {
            ImportController.startDecodingQr()
            if (Qt.platform.os === "ios") {
                PageController.goToPage(PageEnum.PageSetupWizardQrReader)
            }
        }
    }

    QtObject {
        id: restorePurchases

        property string title: qsTr("Restore purchases")
        property string description: qsTr("")
        property string imageSource: "qrc:/images/controls/refresh-cw.svg"
        property bool isVisible: Qt.platform.os === "ios" || IsMacOsNeBuild
        property var handler: function() {
            PageController.showBusyIndicator(true)
            ApiConfigsController.restoreSerivceFromAppStore()
            PageController.showBusyIndicator(false)
        }
    }

    QtObject {
        id: siteLink

        property string title: qsTr("I have nothing")
        property string description: qsTr("")
        property string imageSource: "qrc:/images/controls/help-circle.svg"
        property bool isVisible: PageController.isStartPageVisible() && Qt.platform.os !== "ios" && !IsMacOsNeBuild
        property var handler: function() {
            Qt.openUrlExternally(LanguageModel.getCurrentSiteUrl())
        }
    }

    DrawerType2 {
        id: subscriptionDrawer

        property int configCount: 0

        parent: root
        anchors.fill: parent

        expandedStateContent: ColumnLayout {
            id: subscriptionContent

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0

            Component.onCompleted: {
                subscriptionDrawer.expandedHeight = subscriptionContent.implicitHeight + 32
            }

            Header2Type {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 16

                headerText: qsTr("Subscription loaded")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 24

                text: qsTr("Found %n configuration(s). Add them all?", "", subscriptionDrawer.configCount)
            }

            CheckBoxType {
                id: replacePreviousCheckbox
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 16

                text: qsTr("Delete previous configurations")
                checked: false
                visible: ServersModel.getServersCount() > 0
            }

            BasicButtonType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 8

                text: qsTr("Add %n server(s)", "", subscriptionDrawer.configCount)

                clickedFunc: function() {
                    PageController.showBusyIndicator(true)
                    subscriptionDrawer.closeTriggered()
                    ImportController.importSubscriptionConfigs(replacePreviousCheckbox.checked)
                }
            }

            BasicButtonType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 16

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                textColor: AmneziaStyle.color.paleGray

                text: qsTr("Cancel")

                clickedFunc: function() {
                    subscriptionDrawer.closeTriggered()
                }
            }
        }
    }
}
