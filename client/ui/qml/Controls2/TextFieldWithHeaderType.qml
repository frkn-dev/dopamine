import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Style 1.0

import "TextTypes"

Item {
    id: root

    property string headerText
    property string headerTextDisabledColor: DopamineStyle.color.charcoalGray
    property string headerTextColor: DopamineStyle.color.mutedGray

    property alias errorText: errorField.text
    property bool checkEmptyText: false
    property bool rightButtonClickedOnEnter: false

    property string buttonText
    property string buttonImageSource
    property var clickedFunc

    property alias textField: textField
    property string textFieldTextColor: DopamineStyle.color.paleGray
    property string textFieldTextDisabledColor: DopamineStyle.color.mutedGray

    property bool textFieldEditable: true

    property string borderColor: DopamineStyle.color.slateGray
    property string borderFocusedColor: DopamineStyle.color.paleGray

    property string backgroundColor: DopamineStyle.color.onyxBlack
    property string backgroundDisabledColor: DopamineStyle.color.transparent
    property string bgBorderHoveredColor: DopamineStyle.color.charcoalGray

    implicitWidth: content.implicitWidth
    implicitHeight: content.implicitHeight

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

    ColumnLayout {
        id: content
        anchors.fill: parent

        Rectangle {
            id: backgroud
            Layout.fillWidth: true
            Layout.preferredHeight: input.implicitHeight
            color: root.enabled ? root.backgroundColor : root.backgroundDisabledColor
            radius: 16
            border.color: getBackgroundBorderColor(root.borderColor)
            border.width: 1

            Behavior on border.color {
                PropertyAnimation { duration: 200 }
            }

            RowLayout {
                id: input
                anchors.fill: backgroud
                ColumnLayout {
                    Layout.margins: 16
                    LabelTextType {
                        text: root.headerText
                        color: root.enabled ? root.headerTextColor : root.headerTextDisabledColor

                        visible: text !== ""

                        Layout.fillWidth: true
                    }

                    TextField {
                        id: textField

                        property bool isFocusable: true

                        Keys.onTabPressed: {
                            FocusController.nextKeyTabItem()
                        }

                        Keys.onBacktabPressed: {
                            FocusController.previousKeyTabItem()
                        }

                        enabled: root.textFieldEditable
                        color: root.enabled ? root.textFieldTextColor : root.textFieldTextDisabledColor

                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText

                        placeholderTextColor: DopamineStyle.color.charcoalGray

                        selectionColor:  DopamineStyle.color.richBrown
                        selectedTextColor: DopamineStyle.color.paleGray

                        font.pixelSize: 16
                        font.weight: 400
                        font.family: "IBM Plex Mono"

                        height: 24
                        Layout.fillWidth: true

                        topPadding: 0
                        rightPadding: 0
                        leftPadding: 0
                        bottomPadding: 0

                        background: Rectangle {
                            anchors.fill: parent
                            color: root.backgroundDisabledColor
                        }

                        onTextChanged: {
                            root.errorText = ""
                        }

                        onActiveFocusChanged: {
                            if (root.checkEmptyText && text === "") {
                                root.errorText = qsTr("The field can't be empty")
                            }
                        }

                        ContextMenu.menu: ContextMenuType {
                            textObj: textField
                        }

                        onFocusChanged: {
                            backgroud.border.color = getBackgroundBorderColor(root.borderColor)
                        }
                    }
                }
            }
        }

        SmallTextType {
            id: errorField

            text: root.errorText
            visible: root.errorText !== ""
            color: DopamineStyle.color.vibrantRed

            Layout.fillWidth: true
        }
    }

    MouseArea {
        anchors.fill: root
        cursorShape: Qt.IBeamCursor

        hoverEnabled: true

        onPressed: function(mouse) {
            textField.forceActiveFocus()
            mouse.accepted = false

            backgroud.border.color = getBackgroundBorderColor(root.borderColor)
        }

        onEntered: {
            backgroud.border.color = getBackgroundBorderColor(bgBorderHoveredColor)
        }


        onExited: {
            backgroud.border.color = getBackgroundBorderColor(root.borderColor)
        }
    }

    BasicButtonType {
        visible: (root.buttonText !== "") || (root.buttonImageSource !== "")

        focusPolicy: Qt.NoFocus
        text: root.buttonText
        leftImageSource: root.buttonImageSource

        anchors.top: content.top
        anchors.bottom: content.bottom
        anchors.right: content.right

        height: content.implicitHeight
        width: content.implicitHeight
        squareLeftSide: true

        clickedFunc: function() {
            if (root.clickedFunc && typeof root.clickedFunc === "function") {
                root.clickedFunc()
            }
        }
    }

    function getBackgroundBorderColor(noneFocusedColor) {
        return textField.focus ? root.borderFocusedColor : noneFocusedColor
    }

    Keys.onEnterPressed: {
        if (root.rightButtonClickedOnEnter && root.clickedFunc && typeof root.clickedFunc === "function") {
            clickedFunc()
        }

        // if (KeyNavigation.tab) {
        //     KeyNavigation.tab.forceActiveFocus();
        // }
    }

    Keys.onReturnPressed: {
        if (root.rightButtonClickedOnEnter &&root.clickedFunc && typeof root.clickedFunc === "function") {
            clickedFunc()
        }

        // if (KeyNavigation.tab) {
        //     KeyNavigation.tab.forceActiveFocus();
        // }
    }
}
