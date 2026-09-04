import QtQuick

import Style 1.0

Text {
    lineHeight: 21.6 + LanguageModel.getLineHeightAppend()
    lineHeightMode: Text.FixedHeight

    color: DopamineStyle.color.paleGray
    font.pixelSize: 18
    font.weight: 400
    font.family: "IBM Plex Mono"

    wrapMode: Text.Wrap
}
