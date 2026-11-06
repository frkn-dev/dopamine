import QtQuick
import QtQuick.Layouts

import Style 1.0

BaseHeaderType {
    id: root

    property string actionButtonImage
    property string badgeText
    property var actionButtonFunction
    property alias actionButton: headerActionButton

    Component.onCompleted: {
        headerRow.children.push(headerBadge)
        headerRow.children.push(headerActionButton)
    }

    Rectangle {
        id: headerBadge

        visible: root.badgeText !== ""
        implicitWidth: badgeLabel.implicitWidth + 16
        implicitHeight: 24
        radius: 12
        color: DopamineStyle.color.translucentWhite
        Layout.alignment: Qt.AlignVCenter

        Text {
            id: badgeLabel
            anchors.centerIn: parent
            text: root.badgeText
            color: DopamineStyle.color.paleGray
            font.pixelSize: 12
            font.weight: Font.Medium
        }
    }

    ImageButtonType {
        id: headerActionButton
        implicitWidth: 40
        implicitHeight: 40
        Layout.alignment: Qt.AlignRight
        image: root.actionButtonImage
        imageColor: DopamineStyle.color.paleGray
        visible: image ? true : false

        onClicked: {
            if (actionButtonFunction && typeof actionButtonFunction === "function") {
                actionButtonFunction()
            }
        }
    }

    Keys.onEnterPressed: {
        if (actionButtonFunction && typeof actionButtonFunction === "function") {
            actionButtonFunction()
        }
    }

    Keys.onReturnPressed: {
        if (actionButtonFunction && typeof actionButtonFunction === "function") {
            actionButtonFunction()
        }
    }
} 
