import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

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
        anchors.left: parent.left
        anchors.right: parent.right

        header: ColumnLayout {
            width: listView.width
            spacing: 16

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("VK TURN")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Fallback for hard blocking: WireGuard goes through your VK call TURN relay. Paste your own join link. Keep the call alive — do not end it for everyone. Android 13+ only. Slower than a direct connection.")
            }
        }

        model: 1

        delegate: ColumnLayout {
            width: listView.width
            spacing: 16

            SwitcherType {
                id: enableSwitch

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Use VK TURN")
                descriptionText: qsTr("Applies on the next WireGuard / AmneziaWG connect")

                checked: SettingsController.isVkTurnEnabled
                onToggled: function() {
                    if (checked !== SettingsController.isVkTurnEnabled) {
                        SettingsController.isVkTurnEnabled = checked
                    }
                }
            }

            TextFieldWithHeaderType {
                id: callLinkField

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("VK call link")
                textField.placeholderText: "https://vk.com/call/join/..."
                textField.text: SettingsController.vkCallLink
                textField.onEditingFinished: {
                    if (textField.text !== SettingsController.vkCallLink) {
                        SettingsController.vkCallLink = textField.text.trim()
                    }
                }
            }

            TextFieldWithHeaderType {
                id: peerHostField

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("TURN peer host (optional)")
                textField.placeholderText: qsTr("Empty = VPN server host")
                textField.text: SettingsController.vkTurnPeerHost
                textField.onEditingFinished: {
                    if (textField.text !== SettingsController.vkTurnPeerHost) {
                        SettingsController.vkTurnPeerHost = textField.text.trim()
                    }
                }
            }

            TextFieldWithHeaderType {
                id: peerPortField

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("TURN peer port")
                textField.text: String(SettingsController.vkTurnPeerPort)
                textField.validator: IntValidator { bottom: 1; top: 65535 }
                textField.onEditingFinished: {
                    var port = parseInt(textField.text, 10)
                    if (!isNaN(port) && port !== SettingsController.vkTurnPeerPort) {
                        SettingsController.vkTurnPeerPort = port
                    }
                }
            }

            TextFieldWithHeaderType {
                id: streamsField

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 24

                headerText: qsTr("Parallel streams")
                textField.text: String(SettingsController.vkTurnStreams)
                textField.validator: IntValidator { bottom: 1; top: 12 }
                textField.onEditingFinished: {
                    var n = parseInt(textField.text, 10)
                    if (!isNaN(n) && n !== SettingsController.vkTurnStreams) {
                        SettingsController.vkTurnStreams = n
                    }
                }
            }
        }
    }
}
