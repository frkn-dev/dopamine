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
    property string placeholderText
    property var clickedFunc

    property alias textField: textField
    property string textFieldTextColor: DopamineStyle.color.paleGray
    property string textFieldTextDisabledColor: DopamineStyle.color.mutedGray

    property bool textFieldEditable: true
    property bool multiline: false
    property bool tvInput: false
    readonly property Item editor: multiline ? textArea : textField
    readonly property string inputText: multiline ? textArea.text : textField.text

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
            border.width: root.tvInput && root.editor.activeFocus ? 3 : 1

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

                        visible: !root.multiline
                        property bool isFocusable: true

                        Keys.onPressed: event => {
                            if (root.tvInput) {
                                root.handleTvEditorKey(event)
                            }
                        }

                        Keys.onTabPressed: {
                            FocusController.nextKeyTabItem()
                        }

                        Keys.onBacktabPressed: {
                            FocusController.previousKeyTabItem()
                        }

                        enabled: root.textFieldEditable
                        color: root.enabled ? root.textFieldTextColor : root.textFieldTextDisabledColor

                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText

                        placeholderText: root.placeholderText
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

                    TextArea {
                        id: textArea

                        visible: root.multiline
                        property bool isFocusable: root.tvInput
                        Keys.onPressed: event => {
                            if (root.tvInput) {
                                root.handleTvEditorKey(event)
                            }
                        }
                        enabled: root.textFieldEditable
                        color: root.enabled ? root.textFieldTextColor : root.textFieldTextDisabledColor

                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                        placeholderText: root.placeholderText
                        placeholderTextColor: DopamineStyle.color.charcoalGray
                        selectionColor: DopamineStyle.color.richBrown
                        selectedTextColor: DopamineStyle.color.paleGray

                        font.pixelSize: 16
                        font.weight: 400
                        font.family: "IBM Plex Mono"

                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(160, Math.max(48, contentHeight))
                        rightPadding: insertButton.visible ? insertButton.implicitWidth + 24 : 0
                        topPadding: 0
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
            if (root.multiline) {
                textArea.forceActiveFocus()
            } else {
                textField.forceActiveFocus()
            }
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
        id: insertButton

        visible: (root.buttonText !== "") || (root.buttonImageSource !== "")

        focusPolicy: root.tvInput ? Qt.StrongFocus : Qt.NoFocus
        borderFocusedWidth: root.tvInput ? 3 : 1
        borderFocusedColor: root.tvInput ? DopamineStyle.color.goldenApricot : DopamineStyle.color.paleGray
        text: root.buttonText
        leftImageSource: root.buttonImageSource

        anchors.verticalCenter: root.multiline ? (root.tvInput ? content.verticalCenter : backgroud.verticalCenter) : undefined
        anchors.top: root.multiline ? undefined : content.top
        anchors.bottom: root.multiline ? undefined : content.bottom
        anchors.right: content.right
        anchors.rightMargin: root.multiline ? 8 : 0

        height: root.multiline ? implicitHeight : content.implicitHeight
        width: root.multiline ? implicitWidth : content.implicitHeight
        squareLeftSide: !root.multiline

        clickedFunc: function() {
            if (root.clickedFunc && typeof root.clickedFunc === "function") {
                root.clickedFunc()
            }
        }
    }

    function insertFromClipboard() {
        var field = root.multiline ? textArea : textField
        if (tvInput) {
            // Replace the selection through the editor so an empty clipboard leaves the text intact.
            field.selectAll()
            field.paste()
            return
        }

        field.text = ""
        field.paste()
    }

    function handleTvEditorKey(event) {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Select) {
            if (!event.isAutoRepeat) {
                if (Qt.inputMethod.visible) {
                    Qt.inputMethod.hide()
                } else {
                    Qt.inputMethod.show()
                }
            }
            event.accepted = true
        } else if (!Qt.inputMethod.visible && event.key === Qt.Key_Up) {
            FocusController.nextKeyUpItem()
            event.accepted = true
        } else if (!Qt.inputMethod.visible && event.key === Qt.Key_Down) {
            FocusController.nextKeyDownItem()
            event.accepted = true
        } else if (Qt.inputMethod.visible && (event.key === Qt.Key_Back || event.key === Qt.Key_Escape)) {
            Qt.inputMethod.hide()
            event.accepted = true
        }
    }

    function getBackgroundBorderColor(noneFocusedColor) {
        var focused = root.tvInput ? root.editor.activeFocus : root.editor.focus
        return focused ? root.borderFocusedColor : noneFocusedColor
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
