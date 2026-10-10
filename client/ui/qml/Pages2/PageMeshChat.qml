import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

// Mesh chat: conversation list (ICQ-style). Two transports: BLE mesh for
// nearby peers, relay (sealed E2E) over the internet when the peer is out of
// range. Server never sees plaintext.
PageType {
    id: root

    property bool searchOpen: false
    property string renameUin: ""

    Component.onCompleted: MeshChatController.startMesh()
    Component.onDestruction: MeshChatController.stopMesh()

    Connections {
        target: MeshChatController
        function onContactNotFound() {
            PageController.showNotificationMessage(qsTr("No user with this UIN"))
        }
        function onContactLookupFailed() {
            PageController.showNotificationMessage(qsTr("No internet connection — cannot look up the contact"))
        }
        function onRegisterConflict() {
            PageController.showNotificationMessage(qsTr("This device is already tied to another subscription"))
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

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: 8

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16

                headerText: qsTr("Chat")
                descriptionText: qsTr("No phone number, no email — just your UIN")
            }

            ImageButtonType {
                implicitWidth: 40
                implicitHeight: 40

                image: "qrc:/images/controls/info.svg"
                imageColor: DopamineStyle.color.paleGray

                onClicked: PageController.goToPage(PageEnum.PageMeshAbout)
            }

            ImageButtonType {
                implicitWidth: 40
                implicitHeight: 40

                image: "qrc:/images/controls/search.svg"
                imageColor: DopamineStyle.color.paleGray

                onClicked: root.searchOpen = !root.searchOpen
            }
        }

        LabelWithButtonType {
            Layout.fillWidth: true

            text: qsTr("My UIN")
            descriptionText: MeshChatController.myId ? MeshChatController.myId : qsTr("appears when online")
            rightImageSource: "qrc:/images/controls/copy.svg"

            clickedFunction: function() {
                if (MeshChatController.myId) {
                    MeshChatController.copyMyIdToClipboard()
                    PageController.showNotificationMessage(qsTr("UIN copied"))
                }
            }
        }

        TextField {
            id: myNameInput

            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 8
            implicitHeight: 40

            color: DopamineStyle.color.paleGray
            placeholderText: qsTr("Your name")
            placeholderTextColor: DopamineStyle.color.mutedGray
            text: MeshChatController.myName

            background: Rectangle {
                color: DopamineStyle.color.onyxBlack
                border.color: DopamineStyle.color.slateGray
                border.width: 1
                radius: 8
            }

            onEditingFinished: MeshChatController.setMyName(text)
        }

        // add-contact row, toggled by the search button
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            spacing: 8

            visible: root.searchOpen

            TextField {
                id: contactInput

                Layout.fillWidth: true
                implicitHeight: 40

                color: DopamineStyle.color.paleGray
                placeholderText: qsTr("Contact UIN…")
                placeholderTextColor: DopamineStyle.color.mutedGray
                inputMethodHints: Qt.ImhDigitsOnly

                background: Rectangle {
                    color: DopamineStyle.color.onyxBlack
                    border.color: DopamineStyle.color.slateGray
                    border.width: 1
                    radius: 8
                }

                Keys.onReturnPressed: addButton.clicked()
                Keys.onEnterPressed: addButton.clicked()
            }

            BasicButtonType {
                id: addButton

                implicitHeight: 40
                leftPadding: 16
                rightPadding: 16

                text: qsTr("Add")

                onClicked: {
                    if (contactInput.text.trim() !== "") {
                        MeshChatController.addContact(contactInput.text.trim(), "")
                        contactInput.clear()
                        root.searchOpen = false
                    }
                }
            }
        }

        DividerType {}

        CaptionTextType {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 12
            Layout.bottomMargin: 4

            visible: MeshChatController.nearbyPeers.length > 0
            color: DopamineStyle.color.mutedGray
            text: qsTr("Nearby")
        }

        Repeater {
            model: MeshChatController.nearbyPeers

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: MeshChatController.nearbyPeers.length > 0
                text: modelData.nick
                descriptionText: qsTr("Bluetooth — tap to add")
                rightImageSource: "qrc:/images/controls/plus.svg"

                clickedFunction: function() {
                    MeshChatController.addNearbyPeer(modelData.nick, modelData.fp)
                    MeshChatController.selectedContact = modelData.nick
                    PageController.goToPage(PageEnum.PageMeshConversation)
                }
            }
        }

        ParagraphTextType {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 24

            visible: MeshChatController.contacts.length === 0 && MeshChatController.nearbyPeers.length === 0
            text: qsTr("Add a contact by UIN or find someone nearby")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 8
            spacing: 8

            visible: root.renameUin !== ""

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
                leftPadding: 16
                rightPadding: 16

                text: qsTr("Save")

                onClicked: {
                    MeshChatController.setContactPetName(root.renameUin, petNameInput.text)
                    root.renameUin = ""
                }
            }
        }

        ListViewType {
            id: contactsList

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8

            clip: true
            model: MeshChatController.contacts

            delegate: LabelWithButtonType {
                width: contactsList.width

                text: {
                    var pet = (modelData.name && modelData.name !== modelData.uin) ? modelData.name : ""
                    var server = modelData.serverName || ""
                    var shown = pet || server || modelData.uin
                    return shown !== modelData.uin ? (shown + " · " + modelData.uin) : modelData.uin
                }
                descriptionText: (modelData.lastText ? modelData.lastText : qsTr("No messages yet"))
                    + (modelData.reachable ? " · Bluetooth" : modelData.online ? " · online" : " · off")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    MeshChatController.selectedContact = modelData.uin
                    PageController.goToPage(PageEnum.PageMeshConversation)
                }
                heldFunction: function() {
                    root.renameUin = modelData.uin
                    petNameInput.text = (modelData.name && modelData.name !== modelData.uin) ? modelData.name : ""
                }
            }
        }
    }
}
