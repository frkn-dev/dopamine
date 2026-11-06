import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Config"
import "../Controls2/TextTypes"
import "../Components"

PageType {
    id: root

    // Скролл сбрасываем при открытии страницы, а не по фокусу кнопки «назад»:
    // фокус переезжает во время обычных свайпов, и список возвращало наверх.
    onVisibleChanged: {
        if (visible) {
            listView.positionViewAtBeginning()
        }
    }

    // set when the reload below was started from this page — the completion signal
    // is global, only the initiator shows the result notification
    property bool subscriptionReloadStartedHere: false

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin

    }

    Connections {
        target: ApiConfigsController

        // reloadSubscriptionConfigs is async — the result notification arrives here
        function onReloadSubscriptionConfigsFinished(success) {
            if (!root.subscriptionReloadStartedHere) {
                return
            }
            root.subscriptionReloadStartedHere = false
            PageController.showBusyIndicator(false)
            if (success) {
                PageController.showNotificationMessage(qsTr("Servers reloaded"))
                PageController.goToPageHome()
            } else {
                PageController.showNotificationMessage(qsTr("Failed to reload servers"))
            }
        }
    }

    ListViewType {
        id: listView

        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        header: ColumnLayout {
            width: listView.width

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Application")
            }
        }

        model: 1 // fake model to force the ListView to be created without a model

        delegate: ColumnLayout { // TODO(CyAn84): add DelegateChooser when have migrated to 6.9

            width: listView.width

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 8

                text: qsTr("Subscription ID")
                descriptionText: ApiConfigsController.subscriptionId !== "" ? ApiConfigsController.subscriptionId : qsTr("not set")
                rightImageSource: "qrc:/images/controls/copy.svg"

                clickedFunction: function() {
                    ApiConfigsController.copySubscriptionIdToClipboard()
                    PageController.showNotificationMessage(qsTr("Subscription ID copied"))
                }
            }

            DividerType {}

            LabelWithButtonType {
                id: labelWithButtonLogging

                Layout.fillWidth: true

                text: qsTr("Diagnostics")
                descriptionText: SettingsController.isLoggingEnabled ? qsTr("Enabled") : qsTr("Disabled")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsLogging)
                }
            }

            DividerType {}

            SwitcherType {
                id: switcherAutoServerListUpdate

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Auto-update server list")
                descriptionText: qsTr("Periodically checks the server catalog version and refreshes the list only when it changed on the server.")

                checked: SettingsController.autoServerListUpdateEnabled

                onToggled: function() {
                    if (checked !== SettingsController.autoServerListUpdateEnabled) {
                        SettingsController.autoServerListUpdateEnabled = checked
                    }
                }
            }

            DividerType {}

            SwitcherType {
                id: experimentalSwitcher

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Experimental features")
                descriptionText: qsTr("Experimental features list. Currently one item: service-based split tunneling. May work poorly — enable at your own risk.")

                checked: SettingsController.isExperimentalFeaturesEnabled

                onToggled: function() {
                    if (checked !== SettingsController.isExperimentalFeaturesEnabled) {
                        SettingsController.isExperimentalFeaturesEnabled = checked
                    }
                }
            }

            DividerType {}

            SwitcherType {
                id: endpointTogglesSwitcher

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Allow switch off endpoints")
                descriptionText: qsTr("Adds a per-address switch to the server card, so an address your operator blocks can be turned off. On by default.")

                checked: SettingsController.isEndpointTogglesAllowed

                onToggled: function() {
                    if (checked !== SettingsController.isEndpointTogglesAllowed) {
                        SettingsController.isEndpointTogglesAllowed = checked
                    }
                }
            }

            DividerType {}

            SwitcherType {
                id: postConnectCheckSwitcher

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Check the tunnel after connecting")
                descriptionText: qsTr("Right after a connection is up, resolves a name through the tunnel. " +
                                      "Catches the case where the handshake succeeded but data does not flow. On by default.")

                checked: SettingsController.isPostConnectCheckEnabled

                onToggled: function() {
                    if (checked !== SettingsController.isPostConnectCheckEnabled) {
                        SettingsController.isPostConnectCheckEnabled = checked
                    }
                }
            }

            DividerType {}

            SwitcherType {
                id: reconnectSwitcher

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Keep reconnecting")
                descriptionText: qsTr("If the connection fails or the internet disappears, keep retrying with a growing " +
                                      "delay until you press Cancel — the app reconnects by itself when the network is back. " +
                                      "On by default; when disabled, a failed attempt stops with an error.")

                checked: SettingsController.isReconnectUntilCancelled

                onToggled: function() {
                    if (checked !== SettingsController.isReconnectUntilCancelled) {
                        SettingsController.isReconnectUntilCancelled = checked
                    }
                }
            }

            DividerType {}

            LabelWithButtonType {
                id: labelWithButtonNotification

                visible: Qt.platform.os === "android" && !SettingsController.isNotificationPermissionGranted

                Layout.fillWidth: true

                text: qsTr("Enable notifications")
                descriptionText: qsTr("Enable notifications to show the VPN state in the status bar")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    SettingsController.requestNotificationPermission()
                }
            }

            DividerType {
                visible: Qt.platform.os === "android" && !SettingsController.isNotificationPermissionGranted
            }

            SwitcherType {
                id: switcherAutoStart

                visible: !GC.isMobile()

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Auto start")
                descriptionText: qsTr("Launch the application every time the device is starts")

                checked: SettingsController.isAutoStartEnabled()
                onToggled: function() {
                    if (checked !== SettingsController.isAutoStartEnabled()) {
                        SettingsController.toggleAutoStart(checked)
                    }
                }
            }

            DividerType {
                visible: !GC.isMobile()
            }

            SwitcherType {
                id: switcherAutoConnect

                visible: !GC.isMobile()

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Auto connect")
                descriptionText: qsTr("Connect to VPN on app start")

                checked: SettingsController.isAutoConnectEnabled()
                onToggled: function() {
                    if (checked !== SettingsController.isAutoConnectEnabled()) {
                        SettingsController.toggleAutoConnect(checked)
                    }
                }
            }

            DividerType {
                visible: !GC.isMobile() && !IsMacOsNeBuild
            }

            SwitcherType {
                id: switcherStartMinimized

                visible: !GC.isMobile()

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Start minimized")
                descriptionText: qsTr("Launch application minimized (works with autostart option turned on)")

                enabled: switcherAutoStart.checked
                opacity: enabled ? 1.0 : 0.5

                checked: SettingsController.startMinimized
                onToggled: function() {
                    if (checked !== SettingsController.startMinimized) {
                        SettingsController.toggleStartMinimized(checked)
                    }
                }
            }

            DividerType {
                visible: !GC.isMobile()
            }
        }

        footer: ColumnLayout {

            width: listView.width

            LabelWithButtonType {
                id: labelWithButtonLanguage

                Layout.fillWidth: true

                text: qsTr("Language")
                descriptionText: LanguageModel.currentLanguageName
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    selectLanguageDrawer.openTriggered()
                }
            }

            DividerType {}

            SwitcherType {
                id: switcherDarkMode

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Dark mode")
                descriptionText: qsTr("Use dark mode")

                checked: SettingsController.frknDarkMode()
                onToggled: function() {
                    if (checked !== SettingsController.frknDarkMode()) {
                        SettingsController.toggleFrknDarkMode(checked)
                    }
                    if (SettingsController.frknDarkMode()) {
                        DopamineStyle.color = DopamineStyle.darkColorPalette
                    } else {
                        DopamineStyle.color = DopamineStyle.regularColorPalette
                    }
                }
            }

            DividerType {}

            LabelWithButtonType {
                id: labelWithButtonReloadConfig

                Layout.fillWidth: true

                text: qsTr("Reload all servers from subscription")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    var headerText = qsTr("Reload all servers from subscription?")
                    var descriptionText = qsTr("All servers from the current subscription will be removed and downloaded again. Use this if servers stopped working after an update.")
                    var yesButtonText = qsTr("Reload")
                    var noButtonText = qsTr("Cancel")

                    var yesButtonFunction = function() {
                        if (ConnectionController.isConnected) {
                            PageController.showNotificationMessage(qsTr("Cannot reload configuration during active connection"))
                            return
                        }
                        PageController.showBusyIndicator(true)
                        root.subscriptionReloadStartedHere = true
                        ApiConfigsController.reloadSubscriptionConfigs()
                    }
                    var noButtonFunction = function() {
                    }

                    showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
                }
            }

            DividerType {}

            LabelWithButtonType {
                id: labelWithButtonReset

                Layout.fillWidth: true

                text: qsTr("Reset settings and remove all data from the application")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"
                textColor: DopamineStyle.color.vibrantRed

                clickedFunction: function() {
                    var headerText = qsTr("Reset settings and remove all data from the application?")
                    var descriptionText = qsTr("All settings will be reset to default. All installed Dopamine services will still remain on the server.")
                    var yesButtonText = qsTr("Continue")
                    var noButtonText = qsTr("Cancel")

                    var yesButtonFunction = function() {
                        if (ServersModel.isDefaultServerCurrentlyProcessed() && ConnectionController.isConnected) {
                            PageController.showNotificationMessage(qsTr("Cannot reset settings during active connection"))
                        } else
                        {
                            SettingsController.clearSettings()
                            PageController.goToPageHome()
                        }
                    }
                    var noButtonFunction = function() {
                    }

                    showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
                }
            }

            DividerType {}
        }
    }

    SelectLanguageDrawer {
        id: selectLanguageDrawer

        width: root.width
        height: root.height
    }
}
