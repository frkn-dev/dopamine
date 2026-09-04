import QtQuick

import Style 1.0

Text {
    lineHeight: 16 + LanguageModel.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: DopamineStyle.color.midnightBlack
    font.pixelSize: 13
    font.weight: 400
    font.family: "IBM Plex Mono"
    font.letterSpacing: 0.02

    wrapMode: Text.Wrap
}
