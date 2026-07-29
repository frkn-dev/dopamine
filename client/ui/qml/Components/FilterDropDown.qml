import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Style 1.0

import "../Controls2"
import "../Controls2/TextTypes"

Rectangle {
    id: root

    property var filterModel
    property string currentValue

    signal selected(string value)

    implicitHeight: buttonContent.implicitHeight
    radius: 16

    color: AmneziaStyle.color.onyxBlack
    border.color: popup.opened ? AmneziaStyle.color.goldenApricot : AmneziaStyle.color.slateGray
    border.width: 1

    Behavior on border.color {
        PropertyAnimation { duration: 200 }
    }

    RowLayout {
        id: buttonContent

        anchors.left: parent.left
        anchors.right: parent.right

        spacing: 0

        ButtonTextType {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.topMargin: 16
            Layout.bottomMargin: 16

            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter

            color: AmneziaStyle.color.paleGray
            elide: Text.ElideRight

            text: {
                if (!root.filterModel) {
                    return root.currentValue
                }
                for (let i = 0; i < root.filterModel.count; i++) {
                    const item = root.filterModel.get(i)
                    if (item.value === root.currentValue) {
                        return item.name
                    }
                }
                return root.currentValue
            }
        }

        ImageButtonType {
            Layout.rightMargin: 16

            implicitWidth: 40
            implicitHeight: 40

            hoverEnabled: false
            image: "qrc:/images/controls/chevron-down.svg"
            imageColor: AmneziaStyle.color.goldenApricot

            rotation: popup.opened ? 180 : 0

            Behavior on rotation {
                PropertyAnimation { duration: 200 }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor

        onClicked: {
            if (popup.opened) {
                popup.close()
            } else {
                popup.open()
            }
        }
    }

    Popup {
        id: popup

        y: root.height + 4
        width: root.width
        implicitHeight: Math.min(list.contentHeight + topPadding + bottomPadding, 320)

        topPadding: 4
        bottomPadding: 4
        leftPadding: 4
        rightPadding: 4

        background: Rectangle {
            radius: 16
            color: AmneziaStyle.color.onyxBlack
            border.color: AmneziaStyle.color.charcoalGray
            border.width: 1
        }

        contentItem: ListView {
            id: list

            model: root.filterModel
            clip: true
            interactive: contentHeight > height

            delegate: Rectangle {
                width: list.width
                height: 44
                radius: 12

                color: delegateMouseArea.containsMouse ? AmneziaStyle.color.lightGray : AmneziaStyle.color.transparent

                ButtonTextType {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12

                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight

                    text: name
                    color: value === root.currentValue ? AmneziaStyle.color.goldenApricot : AmneziaStyle.color.paleGray
                }

                MouseArea {
                    id: delegateMouseArea

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true

                    onClicked: {
                        root.selected(value)
                        popup.close()
                    }
                }
            }
        }
    }
}
