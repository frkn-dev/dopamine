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

            headerText: qsTr("Servers")
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
                PageController.showBusyIndicator(true)
                let result = ApiConfigsController.reloadSubscriptionConfigs()
                PageController.showBusyIndicator(false)
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
