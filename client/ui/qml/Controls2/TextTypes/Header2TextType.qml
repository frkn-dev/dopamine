import QtQuick

import Style 1.0

Text {
    lineHeight: 30 + LanguageModel.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: DopamineStyle.color.paleGray
    font.pixelSize: 25
    font.weight: 700
    font.family: "IBM Plex Mono"

    wrapMode: Text.WordWrap
}
