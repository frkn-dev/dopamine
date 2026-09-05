import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import SortFilterProxyModel 0.2

import PageEnum 1.0
import ProtocolEnum 1.0
import ContainerProps 1.0
import ProtocolProps 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    property var processedServer

    Connections {
        target: ServersModel

        function onProcessedServerChanged() {
            root.processedServer = proxyServersModel.get(0)
        }
    }

    SortFilterProxyModel {
        id: proxyServersModel
        objectName: "proxyServersModel"

        sourceModel: ServersModel
        filters: [
            ValueFilter {
                roleName: "isCurrentlyProcessed"
                value: true
            }
        ]

        Component.onCompleted: {
            root.processedServer = proxyServersModel.get(0)
        }
    }

    ColumnLayout {
        objectName: "mainLayout"

        anchors.fill: parent
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin

        spacing: 4

        BackButtonType {
            id: backButton
            objectName: "backButton"
        }

        HeaderTypeWithButton {
            id: headerContent
            objectName: "headerContent"

            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 10

            actionButtonImage: "qrc:/images/controls/edit-3.svg"

            headerText: root.processedServer.name
            descriptionText: {
                if (root.processedServer.isServerFromTelegramApi) {
                    return root.processedServer.serverDescription
                } else if (root.processedServer.hasWriteAccess) {
                    return root.processedServer.credentialsLogin + " · " + root.processedServer.hostName
                } else {
                    return root.processedServer.hostName
                }
            }

            actionButtonFunction: function() {
                serverNameEditDrawer.openTriggered()
            }
        }

        RenameServerDrawer {
            id: serverNameEditDrawer

            parent: root

            anchors.fill: parent
            expandedHeight: root.height * 0.35

            serverNameText: root.processedServer.name
        }

        PageSettingsServerData {
            id: dataPage

            Layout.fillWidth: true
            Layout.fillHeight: true

            stackView: root.stackView
        }
    }
}
