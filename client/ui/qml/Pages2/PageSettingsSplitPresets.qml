import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QtCore

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    property bool pageEnabled

    Component.onCompleted: {
        SplitPresetsModel.fetchPresets()
        // safe to edit while connected — applies on the next connect
        root.pageEnabled = true
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
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            headerText: qsTr("Service-based split tunneling")
            descriptionText: qsTr("Selected services are routed opposite to the default connection: bypass VPN when everything goes through it, or via VPN when the default is direct. Changes apply on the next connection.")
        }

        CaptionTextType {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            visible: ConnectionController.isConnected
            color: AmneziaStyle.color.mutedGray
            text: qsTr("VPN is connected — changes will apply on the next connection")
        }

        CaptionTextType {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.leftMargin: 16
            Layout.rightMargin: 16

            color: AmneziaStyle.color.mutedGray
            text: SitesModel.routeMode === 2
                  ? qsTr("Current direction: everything goes through VPN, selected services bypass it")
                  : qsTr("Current direction: only selected sites and services go through VPN")
        }
    }

    ListViewType {
        id: listView

        anchors.top: header.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom

        width: parent.width

        clip: true

        model: SplitPresetsModel

        delegate: ColumnLayout {
            width: listView.width

            SwitcherType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                enabled: root.pageEnabled

                text: name
                checked: model.enabled

                onToggled: function() {
                    SplitPresetsModel.setPresetEnabled(index, checked)
                }
            }

            DividerType {}
        }
    }

    CaptionTextType {
        anchors.centerIn: parent
        width: parent.width - 32

        visible: SplitPresetsModel.count === 0

        horizontalAlignment: Text.AlignHCenter
        color: AmneziaStyle.color.mutedGray
        text: qsTr("No services available yet. They will appear after the subscription sync.")
    }
}
