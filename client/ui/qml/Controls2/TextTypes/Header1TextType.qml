import QtQuick

import Style 1.0

Text {
    lineHeight: 38 + LanguageModel.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: DopamineStyle.color.paleGray
    font.pixelSize: 32
    font.weight: 700
    font.family: "IBM Plex Mono"
    font.letterSpacing: -1.0

    wrapMode: Text.WordWrap
}

