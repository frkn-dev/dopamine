import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import SortFilterProxyModel 0.2

import PageEnum 1.0
import ProtocolEnum 1.0
import ContainerProps 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    property bool subscriptionReloadStartedHere: false
    // What the reload button is about to refresh, for the result notification.
    property string pendingReloadLabel: ""

    Connections {
        target: ApiConfigsController

        function onReloadSubscriptionConfigsFinished(success) {
            if (!root.subscriptionReloadStartedHere) {
                return
            }
            root.subscriptionReloadStartedHere = false
            PageController.showBusyIndicator(false)
            const scope = root.pendingReloadLabel
            root.pendingReloadLabel = ""
            if (!success) {
                PageController.showNotificationMessage(qsTr("Failed to reload servers"))
            } else if (scope === "") {
                PageController.showNotificationMessage(qsTr("Servers reloaded"))
            } else {
                PageController.showNotificationMessage(qsTr("%1 reloaded").arg(scope))
            }
        }
    }

    RowLayout {
        id: header

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin

        spacing: 0

        BackButtonType {
            id: backButton
        }

        BaseHeaderType {
            Layout.fillWidth: true
            Layout.leftMargin: 16

            headerText: qsTr("Connections List")
        }

        ImageButtonType {
            id: reloadFromApiButton

            Layout.rightMargin: 4

            visible: ServersModel.hasServersFromGatewayApi

            image: "qrc:/images/controls/refresh-cw.svg"
            imageColor: DopamineStyle.color.paleGray

            implicitWidth: 48
            implicitHeight: 48

            onClicked: {
                // "All" reloads everything; a selected subscription reloads only
                // itself. The "Other" bucket has no subscription id of its own, so
                // it falls back to a full reload.
                const selected = servers.subscriptionFilter
                const scoped = selected === "" || selected === servers.otherSubscriptionValue
                root.pendingReloadLabel = scoped ? "" : servers.subscriptionLabel(selected)
                PageController.showBusyIndicator(true)
                root.subscriptionReloadStartedHere = true
                ApiConfigsController.reloadSubscriptionConfigs(false, scoped ? "" : selected)
            }
        }

        ImageButtonType {
            id: addServerButton

            Layout.rightMargin: 12

            image: "qrc:/images/controls/plus.svg"
            imageColor: DopamineStyle.color.paleGray

            implicitWidth: 48
            implicitHeight: 48

            onClicked: PageController.goToPage(PageEnum.PageSetupWizardConfigSource)
        }
    }

    ServersListView {
        id: servers

        objectName: "servers"

        anchors.top: header.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
}
