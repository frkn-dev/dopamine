import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"

PageType {
    id: root

    property bool isAndroid: Qt.platform.os === "android"

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

            headerText: qsTr("Bank and marketplace apps")
            descriptionText: qsTr("Ozon, Wildberries, banks and Gosuslugi detect VPN and may restrict access")
        }
    }

    FlickableType {
        id: fl

        anchors.top: header.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentHeight: content.implicitHeight

        ColumnLayout {
            id: content

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right

            // Android: per-app exclusion hides the VPN from the app entirely —
            // both the local TRANSPORT_VPN check and the server-side IP check pass
            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16

                visible: root.isAndroid
                text: qsTr("On Android: open Settings → Split tunneling → App-based split tunneling and add the app to the list. It will use your regular connection and won't see the VPN at all.")
            }

            // other platforms: only the site-based bypass is available
            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16

                visible: !root.isAndroid
                text: qsTr("Enable the «Online Banking» and/or «RU services» presets in site split tunneling — their traffic will go directly, bypassing the VPN.")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16

                text: qsTr("If an app still refuses to work, the only remaining option is to pause the VPN while using it — some apps detect the VPN interface itself, which cannot be hidden on this platform.")
            }
        }
    }
}
