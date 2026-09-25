import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Config"

PageType {
    id: root

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin

        onActiveFocusChanged: {
            if(backButton.enabled && backButton.activeFocus) {
                listView.positionViewAtBeginning()
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

                headerText: Qt.platform.os === "android" ? qsTr("BelySlon") : qsTr("Connection")
            }
        }

        model: 1 // fake model to force the ListView to be created without a model

        delegate: ColumnLayout { // TODO(CyAn84): add DelegateChooser when have migrated to 6.9

            width: listView.width

            SwitcherType {
                id: amneziaDnsSwitch

                visible: false

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Use DopamineDNS")
                descriptionText: qsTr("If DopamineDNS is installed on the server")

                checked: SettingsController.isAmneziaDnsEnabled()
                onToggled: function() {
                    if (checked !== SettingsController.isAmneziaDnsEnabled()) {
                        SettingsController.toggleAmneziaDns(checked)
                    }
                }
            }

            DividerType {
                visible: false
            }

            SwitcherType {
                id: routeLanSwitch

                visible: !GC.isMobile()

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Route local network through VPN")
                descriptionText: qsTr("When off, devices in your local network (SSH, printers, shared folders) stay reachable while the VPN is on. Applies on the next connection.")

                checked: SettingsController.isRouteLanThroughVpn
                onToggled: function() {
                    if (checked !== SettingsController.isRouteLanThroughVpn) {
                        SettingsController.toggleRouteLanThroughVpn(checked)
                    }
                }
            }

            LabelWithButtonType {
                id: killSwitchButton
                visible: !GC.isMobile()

                Layout.fillWidth: true

                text: qsTr("KillSwitch")
                descriptionText: qsTr("Blocks network connections without VPN")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsKillSwitch)
                }
            }

            DividerType {
                visible: GC.isDesktop()
            }

            LabelWithButtonType {
                id: vkTurnButton
                visible: Qt.platform.os === "android"

                Layout.fillWidth: true

                text: qsTr("VK TURN")
                descriptionText: qsTr("WireGuard via VK call relay (fallback)")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsVkTurn)
                }
            }

            DividerType {
                visible: Qt.platform.os === "android"
            }
        }
    }
}
