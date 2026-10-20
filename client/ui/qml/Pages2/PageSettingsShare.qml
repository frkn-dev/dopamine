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

    property string createdShareUrl: ""
    property string serverName: ""
    property string nodeId: ""
    property string connectionUuid: ""

    function belongsToThisServer(share) {
        if (!share || !share.node_id || share.node_id !== root.nodeId) {
            return false
        }
        if (share.source_connection_id && share.source_connection_id !== root.connectionUuid) {
            return false
        }
        return true
    }

    Component.onCompleted: {
        var apiConfig = ServersModel.getProcessedServerData("apiConfig")
        if (apiConfig) {
            root.nodeId = apiConfig.node_id || ""
            root.connectionUuid = apiConfig.connection_uuid || ""
        }
        root.serverName = ServersModel.getProcessedServerData("name")
        PageController.showBusyIndicator(true)
        ApiConfigsController.listShares()
        PageController.showBusyIndicator(false)
    }

    Connections {
        target: ApiConfigsController

        function onConnectionShareCreated(shareUrl, shareToken) {
            root.createdShareUrl = shareUrl
            labelTextField.textField.text = ""
            PageController.showBusyIndicator(true)
            ApiConfigsController.listShares()
            PageController.showBusyIndicator(false)
        }

        function onSharesListed(shares) {
            sharesModel.clear()
            for (var i = 0; i < shares.length; i++) {
                var share = shares[i]
                if (!root.belongsToThisServer(share)) {
                    continue
                }
                var shareUrl = share.share_url !== undefined && share.share_url !== ""
                        ? share.share_url : "frkn://conn/" + share.share_token
                sharesModel.append({
                    "label": share.label !== undefined ? share.label : "",
                    "shareToken": share.share_token,
                    "shareUrl": shareUrl
                })
            }
        }

        function onShareRevoked(shareToken) {
            for (var i = 0; i < sharesModel.count; i++) {
                if (sharesModel.get(i).shareToken === shareToken) {
                    sharesModel.remove(i)
                    break
                }
            }
            PageController.showNotificationMessage(qsTr("Link revoked"))
        }
    }

    ListModel {
        id: sharesModel
    }

    ListViewType {
        id: listView

        anchors.fill: parent
        anchors.bottomMargin: 24

        model: sharesModel

        header: ColumnLayout {
            width: listView.width

            spacing: 0

            BackButtonType {
                id: backButton

                Layout.topMargin: 20 + SettingsController.safeAreaTopMargin
            }

            BaseHeaderType {
                id: header

                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: qsTr("Share connection")
                descriptionText: qsTr("Only %1. The rest of the subscription stays on your account.").arg(root.serverName)
            }

            TextFieldWithHeaderType {
                id: labelTextField

                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Friend's name")
                placeholderText: qsTr("For example, Mom's phone")
            }

            BasicButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Create link")
                leftImageSource: "qrc:/images/controls/share-2.svg"
                enabled: labelTextField.inputText.trim() !== ""

                clickedFunc: function() {
                    var label = labelTextField.inputText.trim()
                    if (label === "") {
                        PageController.showNotificationMessage(qsTr("Enter a name for this link"))
                        return
                    }
                    PageController.showBusyIndicator(true)
                    ApiConfigsController.shareConnection(ServersModel.processedIndex, label)
                    PageController.showBusyIndicator(false)
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 8

                visible: root.createdShareUrl !== ""

                text: qsTr("Your link is ready")
                descriptionText: root.createdShareUrl
                rightImageSource: "qrc:/images/controls/copy.svg"

                clickedFunction: function() {
                    GC.copyToClipBoard(root.createdShareUrl)
                    PageController.showNotificationMessage(qsTr("Copied"))
                }
            }

            ListItemTitleType {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: sharesModel.count > 0

                text: qsTr("Active links")
            }

            CaptionTextType {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: sharesModel.count === 0

                color: DopamineStyle.color.mutedGray
                text: qsTr("You haven't shared this connection yet")
            }
        }

        delegate: ColumnLayout {
            width: listView.width

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                Layout.bottomMargin: 8

                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true

                    spacing: 0

                    ListItemTitleType {
                        Layout.fillWidth: true

                        text: label !== "" ? label : qsTr("Shared link")
                        elide: Text.ElideRight
                        maximumLineCount: 2
                    }

                    CaptionTextType {
                        Layout.fillWidth: true

                        text: shareUrl
                        color: DopamineStyle.color.mutedGray
                    }
                }

                ImageButtonType {
                    implicitWidth: 40
                    implicitHeight: 40

                    Layout.alignment: Qt.AlignRight

                    image: "qrc:/images/controls/copy.svg"
                    imageColor: DopamineStyle.color.paleGray

                    onClicked: {
                        GC.copyToClipBoard(shareUrl)
                        PageController.showNotificationMessage(qsTr("Copied"))
                    }
                }

                ImageButtonType {
                    implicitWidth: 40
                    implicitHeight: 40

                    Layout.alignment: Qt.AlignRight

                    image: "qrc:/images/controls/trash.svg"
                    imageColor: DopamineStyle.color.paleGray

                    onClicked: {
                        var headerText = qsTr("Revoke this link?")
                        var descriptionText = qsTr("The person you shared this connection with will immediately lose access to it")
                        var yesButtonText = qsTr("Continue")
                        var noButtonText = qsTr("Cancel")

                        var yesButtonFunction = function() {
                            PageController.showBusyIndicator(true)
                            ApiConfigsController.revokeShare(shareToken)
                            PageController.showBusyIndicator(false)
                        }
                        var noButtonFunction = function() {
                        }

                        showQuestionDrawer(headerText, descriptionText, yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
                    }
                }
            }

            DividerType {}
        }
    }
}
