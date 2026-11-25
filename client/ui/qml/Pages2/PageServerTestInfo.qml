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
            Layout.bottomMargin: 8

            headerText: qsTr("How the measurement works")
            descriptionText: qsTr("Two numbers per node: the handshake time and the download speed from the node itself")
        }
    }

    FlickableType {
        id: fl

        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        contentHeight: content.implicitHeight

        ColumnLayout {
            id: content

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0

            Header2TextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 16
                Layout.bottomMargin: 6

                text: qsTr("Handshake, ms")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 20

                text: qsTr("A real connection attempt to the node with the same protocol the app uses. Three tries, the average is shown. It answers the question \"can I connect to this node right now\", and not \"is the node listed as online\". Lower is better.")
            }

            Header2TextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 6

                text: qsTr("Speed, Mbps")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8

                text: qsTr("A download from the node itself in three rounds, averaged. It is the node uplink: not your provider's speed and not the speed through the tunnel. Higher is better.")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 20

                text: qsTr("The label under the number says what that speed is enough for: 4K needs about 50 Mbps per stream, HD about 10-25, below that it is web and messengers only. A node with a handshake under 60 ms is also marked as suitable for gaming.")
            }

            Header2TextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 6

                text: qsTr("Jitter, ms")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 20

                text: qsTr("How much the answer time varies: the spread between the fastest and the slowest of the three tries. The average says \"70 ms\", jitter says how far those 70 wander. At 60 ms with 200 ms of jitter a call breaks up even though the ping looks good, so a steady 80 ms is better than a jumpy 40. Lower is better.")
            }

            Header2TextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 6

                text: qsTr("Loss")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 20

                text: qsTr("How many of the three tries got no answer. \"Lost 1 of 3\" means the node is unstable: it accepts connections but drops part of them. Lower is better.")
            }

            Header2TextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 6

                text: qsTr("Why the VPN has to be off")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 32

                text: qsTr("The test runs from your device to the node directly. Through a tunnel both numbers would describe the exit server, not the path to the node you are choosing.")
            }
        }
    }
}
