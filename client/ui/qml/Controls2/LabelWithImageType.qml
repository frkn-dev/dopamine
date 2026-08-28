import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Style 1.0

import "TextTypes"

RowLayout {
    id: root

    property string imageSource
    property string leftText
    property var rightText
    property bool isRightTextUndefined: rightText === undefined
    property int rightTextFormat: Text.PlainText

    visible: !isRightTextUndefined

    Image {
        id: iconImage

        Layout.preferredHeight: 18
        Layout.preferredWidth: 18
        source: root.imageSource
    }

    ListItemTitleType {
        id: titleItem

        Layout.fillWidth: true
        Layout.rightMargin: 10
        Layout.alignment: Qt.AlignRight

        text: root.leftText
    }

    ParagraphTextType {
        visible: root.rightText !== ""

        Layout.alignment: Qt.AlignLeft
        // cap the value's width: an unconstrained long value claims its full
        // unwrapped implicit width, collapses the title to 0 and overflows the
        // row past the screen edge (short values end up painted off-screen too)
        Layout.maximumWidth: Math.max(root.width - iconImage.width - titleItem.implicitWidth - root.spacing * 2, 0)

        horizontalAlignment: Text.AlignRight
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere

        text: root.isRightTextUndefined ? "" : root.rightText
        textFormat: root.rightTextFormat
    }
}
