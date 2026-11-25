import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

import Style 1.0

import "TextTypes"

RadioButton {
    id: root

    property int textMaximumLineCount: 2
    // Shrink the title before it gets elided (see titleText.font.pixelSize).
    property bool textAutoFit: false

    TextMetrics {
        id: titleMetrics

        font.family: "IBM Plex Mono"
        font.weight: 400
        font.pixelSize: 18
        text: root.text
    }
    property int textElide: Qt.ElideRight
    property string descriptionText

    property string hoveredColor: DopamineStyle.color.barelyTranslucentWhite
    property string defaultColor: DopamineStyle.color.transparent
    property string disabledColor: DopamineStyle.color.transparent
    property string selectedColor: DopamineStyle.color.transparent

    property string textColor: DopamineStyle.color.paleGray
    property string textDisabledColor: DopamineStyle.color.mutedGray
    property string selectedTextColor: DopamineStyle.color.goldenApricot
    property string selectedTextDisabledColor: DopamineStyle.color.burntOrange
    property string descriptionColor: DopamineStyle.color.mutedGray
    property string descriptionDisabledColor: DopamineStyle.color.charcoalGray

    property string borderFocusedColor: DopamineStyle.color.paleGray
    property int borderFocusedWidth: 1

    property string imageSource
    property bool showImage

    // optional flag shown left of the title (auto-select)
    property string leadingImageSource
    property int leadingImageSize: 24

    property bool isFocusable: true

    
    property string radioButtonInnerCirclePressedSource: "qrc:/images/controls/radio-button-inner-circle-pressed.png"
    // Colour for the checked dot (see the Rectangle below). Empty = use the PNG.
    property string innerCircleColor: ""
    // Colour the picked row is drawn in when the caller gave none: white.
    readonly property string selectionColor: innerCircleColor !== "" ? innerCircleColor : "#E6E8EF"
    property string radioButtonInnerCircleSource: "qrc:/images/controls/radio-button-inner-circle.png"
    property string radioButtonPressedSource: "qrc:/images/controls/radio-button-pressed.svg"
    property string radioButtonDefaultSource: "qrc:/images/controls/radio-button.svg"

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

    hoverEnabled: true

    indicator: Rectangle {
        id: background

        anchors.verticalCenter: parent.verticalCenter

        border.color: root.focus ? root.borderFocusedColor : DopamineStyle.color.transparent
        border.width: root.focus ? root.borderFocusedWidth : 0

        implicitWidth: 56
        implicitHeight: 56
        radius: 16

        color: {
            if (root.enabled) {
                if (root.hovered) {
                    return hoveredColor
                } else if (root.checked) {
                    return selectedColor
                }
                return defaultColor
            } else {
                return disabledColor
            }
        }

        Behavior on color {
            PropertyAnimation { duration: 200 }
        }

        Behavior on border.color {
            PropertyAnimation { duration: 200 }
        }

        Image {
            source: {
                if (showImage) {
                    return imageSource
                } else if (root.checked || root.pressed) {
                    // Selection is drawn from `selectionColor` below: white for a
                    // picked row, whatever the caller passes for a connected one.
                    // The old blue PNG (radio-button-inner-circle.png) leaked
                    // through whenever the caller had no colour to give.
                    return ""
                }

                return ""
            }

            opacity: root.enabled ? 1.0 : 0.3
            anchors.centerIn: parent

            width: 24
            height: 24
        }

        // Solid dot for callers that colour the selection themselves: green while
        // the tunnel is up, plain white for "selected but not connected".
        // White by default: "no tunnel" must read as a plain picked row, never
        // as the legacy blue dot. Callers pass a colour only for the connected one.
        Rectangle {
            visible: root.checked && !showImage
            anchors.centerIn: parent
            width: 24
            height: 24
            radius: 12
            color: "transparent"
            border.width: 1
            border.color: root.selectionColor
        }

        Rectangle {
            visible: root.checked && !showImage
            anchors.centerIn: parent
            width: 12
            height: 12
            radius: 6
            color: root.selectionColor
        }

        Image {
            source: {
                if (showImage) {
                    return ""
                } else if (root.checked && root.innerCircleColor !== "") {
                    // Coloured selection (green = connected, white = selected):
                    // the ring is drawn from innerCircleColor below.
                    return ""
                } else if (root.pressed || root.checked) {
                    return root.radioButtonPressedSource
                } else {
                    return root.radioButtonDefaultSource
                }
            }

            opacity: root.enabled ? 1.0 : 0.3
            anchors.centerIn: parent

            width: 24
            height: 24
        }
    }

    contentItem: Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 8 + background.width

        implicitHeight: content.implicitHeight

        RowLayout {
            id: content

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            spacing: 8

            Image {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: root.leadingImageSize
                Layout.preferredHeight: root.leadingImageSize

                visible: root.leadingImageSource !== ""
                source: root.leadingImageSource
                fillMode: Image.PreserveAspectFit

                layer.enabled: true
                layer.effect: OpacityMask {
                    maskSource: Rectangle {
                        width: root.leadingImageSize
                        height: root.leadingImageSize
                        radius: 5
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true

                spacing: 4

                ListItemTitleType {
                    id: titleText

                    text: root.text
                    maximumLineCount: root.textMaximumLineCount
                    elide: root.textElide

                    // Shrink to fit instead of truncating: a long pool label
                    // ("Czechia | Selfhosted") must stay readable next to the
                    // ping digits, so the font gives up a couple of points
                    // before the text starts losing characters.
                    font.pixelSize: root.textAutoFit
                                     ? Math.max(13, Math.min(18,
                                            Math.floor(18 * Math.max(0, width - 2)
                                                       / Math.max(1, titleMetrics.advanceWidth))))
                                     : 18

                    color: {
                        if (root.enabled) {
                            return root.checked ? selectedTextColor : textColor
                        } else {
                            return root.checked ? selectedTextDisabledColor : textDisabledColor
                        }
                    }

                    Layout.fillWidth: true

                    Behavior on color {
                        PropertyAnimation { duration: 200 }
                    }
                }

                CaptionTextType {
                    id: description

                    color: root.enabled ? root.descriptionColor : root.descriptionDisabledColor
                    text: root.descriptionText

                    visible: root.descriptionText !== ""

                    Layout.fillWidth: true
                }
            }
        }
    }

    MouseArea {
        anchors.fill: root
        cursorShape: Qt.PointingHandCursor
        preventStealing: false
        enabled: false
    }
}


