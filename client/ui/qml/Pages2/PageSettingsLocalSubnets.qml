import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QtCore

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

    property bool pageEnabled: true

    Connections {
        target: LocalSubnetExclusionsController

        function onFinished(message) {
            PageController.showNotificationMessage(message)
        }

        function onErrorOccurred(errorMessage) {
            PageController.showErrorMessage(errorMessage)
        }
    }

    ColumnLayout {
        id: header

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin

        BackButtonType {
            id: backButton
        }

        BaseHeaderType {
            enabled: root.pageEnabled

            Layout.fillWidth: true
            Layout.leftMargin: 16

            headerText: qsTr("Local subnets")
            descriptionText: qsTr("Subnets listed here always bypass the VPN — they stay reachable even when the kill switch is on. The change is applied on the next connection.")
        }
    }

    ListView {
        id: listView

        anchors.top: header.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom

        width: parent.width

        enabled: root.pageEnabled

        property bool isFocusable: true

        cacheBuffer: 200
        displayMarginBeginning: 40
        displayMarginEnd: 40

        ScrollBar.vertical: ScrollBarType {}

        footer: Item {
            width: listView.width
            height: addSubnetPanel.height
        }

        footerPositioning: ListView.InlineFooter

        model: SortFilterProxyModel {
            id: subnetsFilterModel
            sourceModel: LocalSubnetExclusionsModel
            filters: [
                RegExpFilter {
                    roleName: "subnet"
                    pattern: ".*" + addSubnetPanel.textField.text + ".*"
                    caseSensitivity: Qt.CaseInsensitive
                }
            ]
        }

        clip: true

        reuseItems: true

        delegate: ColumnLayout {
            id: delegateContent

            width: listView.width

            LabelWithButtonType {
                id: subnetItem
                Layout.fillWidth: true

                text: subnet
                rightImageSource: "qrc:/images/controls/trash.svg"
                rightImageColor: DopamineStyle.color.paleGray

                clickedFunction: function() {
                    var headerText = qsTr("Delete ") + subnet + "?"
                    var yesButtonText = qsTr("Continue")
                    var noButtonText = qsTr("Cancel")

                    var yesButtonFunction = function() {
                        LocalSubnetExclusionsController.removeSubnet(subnetsFilterModel.mapToSource(index))
                        if (!GC.isMobile()) {
                            subnetItem.rightButton.forceActiveFocus()
                        }
                    }
                    var noButtonFunction = function() {
                        if (!GC.isMobile()) {
                            subnetItem.rightButton.forceActiveFocus()
                        }
                    }

                    showQuestionDrawer(headerText, "", yesButtonText, noButtonText, yesButtonFunction, noButtonFunction)
                }
            }

            DividerType {}
        }
    }

    AddSitePanel {
        id: addSubnetPanel

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        enabled: root.pageEnabled
        placeholderText: qsTr("192.168.0.0/16 or 10.0.0.1")

        onAddClicked: function(text) {
            PageController.showBusyIndicator(true)
            LocalSubnetExclusionsController.addSubnet(text)
            PageController.showBusyIndicator(false)
        }

        onMoreClicked: {
            moreActionsDrawer.openTriggered()
        }
    }

    DrawerType2 {
        id: moreActionsDrawer

        anchors.fill: parent
        expandedHeight: parent.height * 0.35

        expandedStateContent: ColumnLayout {
            id: moreActionsDrawerContent

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            Header2Type {
                Layout.fillWidth: true
                Layout.margins: 16

                headerText: qsTr("Local subnets")
            }

            LabelWithButtonType {
                id: clearSubnetsButton
                Layout.fillWidth: true

                text: qsTr("Clear the list")

                clickedFunction: function() {
                    LocalSubnetExclusionsController.removeSubnets()
                    moreActionsDrawer.closeTriggered()
                }
            }

            DividerType {}
        }
    }
}
