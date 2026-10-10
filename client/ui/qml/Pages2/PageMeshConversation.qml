import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

// Mesh chat: single conversation with the contact chosen on PageMeshChat.
PageType {
    id: root

    property bool editingPet: false

    Connections {
        target: MeshChatController
        function onPeerUnreachable() {
            PageController.showNotificationMessage(qsTr("Peer is unreachable: not in Bluetooth range and no internet"))
        }
    }

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin
    }

    ColumnLayout {
        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottomMargin: 16
        spacing: 0

        BaseHeaderType {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            headerText: MeshChatController.selectedContactName
            descriptionText: MeshChatController.selectedContactStatus === "ble" ? qsTr("via Bluetooth mesh")
                : MeshChatController.selectedContactStatus === "online" ? qsTr("online — via relay")
                : qsTr("offline")

            MouseArea {
                anchors.fill: parent
                onPressAndHold: root.editingPet = !root.editingPet
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            visible: root.editingPet

            TextField {
                id: petNameInput

                Layout.fillWidth: true
                implicitHeight: 40

                color: DopamineStyle.color.paleGray
                placeholderText: qsTr("Local name, only on this device")
                placeholderTextColor: DopamineStyle.color.mutedGray

                background: Rectangle {
                    color: DopamineStyle.color.onyxBlack
                    border.color: DopamineStyle.color.slateGray
                    border.width: 1
                    radius: 8
                }

                Keys.onReturnPressed: petSave.clicked()
                Keys.onEnterPressed: petSave.clicked()
            }

            BasicButtonType {
                id: petSave

                implicitHeight: 40
                text: qsTr("Save")

                onClicked: {
                    MeshChatController.setContactPetName(MeshChatController.selectedContact, petNameInput.text)
                    root.editingPet = false
                }
            }
        }

        DividerType {}

        ListViewType {
            id: messagesList

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8

            clip: true
            model: MeshChatController.messages

            onCountChanged: positionViewAtEnd()

            delegate: ColumnLayout {
                width: messagesList.width
                spacing: 2

                CaptionTextType {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16

                    color: modelData.isMine ? DopamineStyle.color.goldenApricot : DopamineStyle.color.mutedGray
                    text: (modelData.isMine ? qsTr("me") : (modelData.sender || "?")) + " · " + (modelData.time || "")
                          + (modelData.transport === "ble" ? " · Bluetooth" : modelData.transport === "relay" ? " · relay" : "")
                          + (modelData.isMine && !modelData.delivered ? " · " + qsTr("not delivered") : "")
                }

                ParagraphTextType {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 8

                    text: modelData.text || ""
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            TextField {
                id: messageInput

                Layout.fillWidth: true

                color: DopamineStyle.color.paleGray
                placeholderText: qsTr("Message")
                placeholderTextColor: DopamineStyle.color.mutedGray

                background: Rectangle {
                    color: DopamineStyle.color.onyxBlack
                    border.color: messageInput.activeFocus ? DopamineStyle.color.paleGray : DopamineStyle.color.slateGray
                    border.width: 1
                    radius: 8
                }

                Keys.onReturnPressed: sendButton.clicked()
                Keys.onEnterPressed: sendButton.clicked()
            }

            BasicButtonType {
                id: sendButton

                implicitHeight: 48
                leftPadding: 20
                rightPadding: 20

                text: qsTr("Send")

                onClicked: {
                    MeshChatController.sendMessage(messageInput.text)
                    messageInput.clear()
                }
            }
        }
    }
}
