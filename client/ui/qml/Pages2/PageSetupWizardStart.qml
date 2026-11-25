import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Config"
import "../Controls2/TextTypes"
import "../Components"

PageType {
    id: root

    readonly property bool isTv: SettingsController.isOnTv()

    enableTimer: !isTv

    StackView.onActivated: {
        if (root.isTv) {
            Qt.callLater(focusStartButton)
        }
    }

    function focusStartButton() {
        if (StackView.status === StackView.Active) {
            FocusController.resetRootObject()
            FocusController.setFocusItem(startButton)
        }
    }

    ColumnLayout {
        id: content

        anchors.fill: parent
        anchors.leftMargin: root.isTv ? 32 : 0
        anchors.rightMargin: root.isTv ? 32 : 0
        spacing: root.isTv ? 16 : 0

        // The mascot leads the start screen as a round avatar; the square
        // stretched logo read as a banner, not as the app's face.
        Item {
            id: mascotFrame

            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.topMargin: (root.isTv ? 16 : 32) + SettingsController.safeAreaTopMargin
            Layout.preferredWidth: root.isTv ? Math.min(content.width, 360) : 220
            Layout.preferredHeight: root.isTv ? Math.min(root.height * 0.5, 248) : 220

            Rectangle {
                id: mascotDisc

                anchors.fill: parent
                radius: width / 2
                color: "#ffffff"
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.08)
            }

            Rectangle {
                id: mascotMask

                anchors.fill: parent
                radius: width / 2
                color: "white"
                visible: false
            }

            Image {
                id: mascotLogo

                anchors.fill: parent
                source: "qrc:/images/pterodactyl.png"
                // Cap the decode size: the 1024x1024 mascot was ~4 MB per load.
                sourceSize: Qt.size(440 * Screen.devicePixelRatio, 440 * Screen.devicePixelRatio)
                fillMode: Image.PreserveAspectCrop
                visible: false
            }

            OpacityMask {
                anchors.fill: parent
                source: mascotLogo
                maskSource: mascotMask
            }
        }

        BasicButtonType {
            id: startButton
            Layout.fillWidth: true
            Layout.maximumWidth: root.isTv ? 440 : Number.POSITIVE_INFINITY
            Layout.bottomMargin: (root.isTv ? 32 : 48) + SettingsController.safeAreaBottomMargin
            Layout.leftMargin: root.isTv ? 0 : 16
            Layout.rightMargin: root.isTv ? 0 : 16
            Layout.alignment: root.isTv ? Qt.AlignHCenter | Qt.AlignBottom : Qt.AlignBottom

            implicitHeight: root.isTv ? 64 : 56
            borderFocusedColor: root.isTv ? DopamineStyle.color.goldenApricot : DopamineStyle.color.paleGray
            borderFocusedWidth: root.isTv ? 3 : 1
            buttonTextLabel.font.pixelSize: root.isTv ? 22 : 16

            text: qsTr("Let's get started")

            clickedFunc: function() {
                if (ServersModel.getServersCount() > 0) {
                    PageController.goToPageHome()
                } else {
                    PageController.goToPage(PageEnum.PageSetupWizardConfigSource)
                }
            }
        }
    }
}
