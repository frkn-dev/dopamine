import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

import Style 1.0

import "TextTypes"

CheckBox {
    id: root

    property string descriptionText
    property string descriptionTextColor: DopamineStyle.color.mutedGray
    property string descriptionTextDisabledColor: DopamineStyle.color.charcoalGray

    property string textColor: DopamineStyle.color.paleGray
    property string textDisabledColor: DopamineStyle.color.mutedGray

    property string hoveredColor: DopamineStyle.color.barelyTranslucentWhite
    property string defaultColor: DopamineStyle.color.transparent
    property string pressedColor: DopamineStyle.color.barelyTranslucentWhite

    property string defaultBorderColor: DopamineStyle.color.paleGray
    property string checkedBorderColor: DopamineStyle.color.goldenApricot
    property string checkedBorderDisabledColor: DopamineStyle.color.deepBrown

    property string borderFocusedColor: DopamineStyle.color.paleGray

    property string checkedImageColor: DopamineStyle.color.goldenApricot
    property string pressedImageColor: DopamineStyle.color.burntOrange
    property string defaultImageColor: DopamineStyle.color.transparent
    property string checkedDisabledImageColor: DopamineStyle.color.mutedBrown

    property string imageSource: "qrc:/images/controls/check.svg"

    property bool isFocusable: true

    Keys.onTabPressed: {
        FocusController.nextKeyTabItem()
    }

    Keys.onBacktabPressed: {
        FocusController.previousKeyTabItem()
    }

    Keys.onUpPressed: {
        FocusController.nextKeyUpItem()
    }
    
    Keys.onDownPressed: {
        FocusController.nextKeyDownItem()
    }
    
    Keys.onLeftPressed: {
        FocusController.nextKeyLeftItem()
    }

    Keys.onRightPressed: {
        FocusController.nextKeyRightItem()
    }

    hoverEnabled: enabled ? true : false
    focusPolicy: Qt.NoFocus

    background: Rectangle {
        color: DopamineStyle.color.transparent
        border.color: root.focus ? borderFocusedColor : DopamineStyle.color.transparent
        border.width: 1
        radius: 16
    }

    indicator: Rectangle {
        id: background

        anchors.verticalCenter: parent.verticalCenter

        implicitWidth: 56
        implicitHeight: 56
        radius: 16

        color:  {
            if (root.hovered) {
                return hoveredColor
            }
            return defaultColor
        }

        Behavior on color {
            PropertyAnimation { duration: 200 }
        }

        Rectangle {
            id: imageBorder

            anchors.centerIn: parent
            width: 24
            height: 24
            color: DopamineStyle.color.transparent
            border.color: root.checked ?
                              (root.enabled ?
                                   checkedBorderColor :
                                   checkedBorderDisabledColor) :
                              defaultBorderColor
            border.width: 1
            radius: 4

            Image {
                anchors.centerIn: parent

                source: root.pressed ? imageSource : root.checked ? imageSource : ""
                layer {
                    enabled: true
                    effect: ColorOverlay {
                        color: {
                            if (root.pressed) {
                                return root.pressedImageColor
                            } else if (root.checked) {
                                if (root.enabled) {
                                    return root.checkedImageColor
                                } else {
                                    return root.checkedDisabledImageColor
                                }
                            } else {
                                return root.defaultImageColor
                            }
                        }
                    }
                }
            }
        }
    }

    contentItem: Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 8 + background.width

        implicitHeight: content.implicitHeight

        ColumnLayout {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            spacing: 4

            ListItemTitleType {
                Layout.fillWidth: true

                text: root.text
                color: root.enabled ? root.textColor : root.textDisabledColor
            }

            CaptionTextType {
                id: description

                Layout.fillWidth: true

                text: root.descriptionText
                color: root.enabled ? root.descriptionTextColor : root.descriptionTextDisabledColor

                visible: root.descriptionText !== ""
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        enabled: false
    }

    Keys.onEnterPressed: event => handleSwitch(event)
    Keys.onReturnPressed: event => handleSwitch(event)
    Keys.onSpacePressed: event => handleSwitch(event)

    function handleSwitch(event) {
        if (!event.isAutoRepeat) {
            root.checked = !root.checked
            root.checkedChanged()
        }
        event.accepted = true
    }
}


