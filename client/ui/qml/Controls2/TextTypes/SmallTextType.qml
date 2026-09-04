import QtQuick

import Style 1.0

Text {
    lineHeight: 20 + LanguageModel.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: DopamineStyle.color.paleGray
    font.pixelSize: 14
    font.weight: 400
    font.family: "IBM Plex Mono"

    wrapMode: Text.WordWrap
}
