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

    Component.onCompleted: function() {
        // Force a fresh read: the list may hold rows of a subscription that
        // was replaced since the last visit, and the copy button then hands
        // out the old id.
        SubscriptionsInfoController.refresh()
    }

    BusyIndicatorType {
        id: busyIndicator

        visible: SubscriptionsInfoController.busy
    }

    ListViewType {
        id: listView

        anchors.fill: parent
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin
        anchors.bottomMargin: addSubscriptionBar.height + 24

        model: SubscriptionsInfoController.subscriptions

        header: ColumnLayout {
            width: listView.width

            BackButtonType {
                id: backButton
            }

            BaseHeaderType {
                id: header

                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: qsTr("Subscription")
                descriptionText: qsTr("Every subscription this device is linked to")
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: !SubscriptionsInfoController.busy && listView.count === 0

                text: qsTr("No subscriptions found on this device")
                color: DopamineStyle.color.mutedGray
                font.pixelSize: 15
                wrapMode: Text.WordWrap
            }
        }

        delegate: ColumnLayout {
            width: listView.width
            spacing: 0

            // Subscription id: the whole thing is what support asks for, so a
            // tap copies it instead of truncating it in the UI.
            LabelWithButtonType {
                Layout.fillWidth: true

                text: qsTr("Subscription ID")
                descriptionText: modelData.id
                descriptionOnTop: true

                rightImageSource: "qrc:/images/controls/copy.svg"

                clickedFunction: function() {
                    SubscriptionsInfoController.copyToClipboard(modelData.id)
                    PageController.showNotificationMessage(qsTr("Copied"))
                }
            }

            DividerType {
                visible: modelData.code !== ""
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: modelData.code !== ""

                text: qsTr("Short code")
                descriptionText: modelData.code
                descriptionOnTop: true

                rightImageSource: "qrc:/images/controls/copy.svg"

                clickedFunction: function() {
                    SubscriptionsInfoController.copyToClipboard(modelData.code)
                    PageController.showNotificationMessage(qsTr("Copied"))
                }
            }

            DividerType {
                visible: modelData.planTitle !== ""
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: modelData.planTitle !== ""

                text: qsTr("Plan")
                descriptionText: modelData.planTitle
                descriptionOnTop: true
            }

            DividerType {
                visible: modelData.expiresText !== ""
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: modelData.expiresText !== ""

                text: qsTr("Valid until")
                descriptionText: modelData.expired
                                 ? modelData.expiresText + " · " + qsTr("expired")
                                 : modelData.expiresText
                descriptionOnTop: true
                descriptionColor: modelData.expired
                                  ? DopamineStyle.color.richBrown
                                  : DopamineStyle.color.mutedGray
            }

            DividerType {
                visible: modelData.hasTraffic
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: modelData.hasTraffic

                text: qsTr("Traffic")
                descriptionText: modelData.limitText !== ""
                                 ? modelData.usedText + " / " + modelData.limitText
                                 : modelData.usedText + " / " + qsTr("unlimited")
                descriptionOnTop: true
            }

            DividerType {}
        }
    }

    // Adding another subscription is a first-class action on this page: the
    // same key/link/config entry screen the "+" on the connections list opens.
    Rectangle {
        id: addSubscriptionBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        height: addSubscriptionButton.implicitHeight + 48 + SettingsController.safeAreaBottomMargin

        color: DopamineStyle.color.midnightBlack

        BasicButtonType {
            id: addSubscriptionButton

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 24 + SettingsController.safeAreaBottomMargin

            text: qsTr("Add subscription")

            clickedFunc: function() {
                PageController.goToPage(PageEnum.PageSetupWizardConfigSource)
            }
        }
    }
}
