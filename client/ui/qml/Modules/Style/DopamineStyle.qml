pragma Singleton

import QtQuick

QtObject {
    property QtObject regularColorPalette: QtObject {
        readonly property color transparent: 'transparent'
        readonly property color paleGray: '#121314'
        readonly property color lightGray: '#2A2B2C'
        readonly property color mutedGray: '#686A6D'
        readonly property color charcoalGray: '#ABACAD'
        readonly property color slateGray: '#DEDFE0'
        readonly property color onyxBlack: '#EFEFEF'
        readonly property color midnightBlack: '#FFFFFF'
        readonly property color goldenApricot: '#2757FF'
        readonly property color burntOrange: '#002BC4'
        readonly property color mutedBrown: '#002BC4'
        readonly property color richBrown: '#2757FF'
        readonly property color deepBrown: '#2757FF'
        readonly property color vibrantRed: '#9B0104'
        readonly property color darkCharcoal: '#E5E5E6'
        readonly property color sheerWhite: Qt.rgba(0, 0, 0, 0.12)
        readonly property color translucentWhite: Qt.rgba(0, 0, 0, 0.08)
        readonly property color barelyTranslucentWhite: Qt.rgba(0, 0, 0, 0.05)
        readonly property color translucentMidnightBlack: Qt.rgba(1, 1, 1, 0.8)
        readonly property color softGoldenApricot: Qt.rgba(39/255, 87/255, 255/255, 0.3)
        readonly property color mistyGray: Qt.rgba(40/255, 39/255, 36/255, 0.8)
        readonly property color cloudyGray: Qt.rgba(40/255, 39/255, 36/255, 0.65)
        readonly property color pearlGray: '#151513'
        readonly property color translucentRichBrown: Qt.rgba(39/255, 87/255, 255/255, 0.26)
        readonly property color translucentSlateGray: Qt.rgba(222/255, 223/255, 224/255, 0.13)
        readonly property color translucentOnyxBlack: Qt.rgba(239/255, 239/255, 239/255, 0.13)
    }

    property QtObject darkColorPalette: QtObject {
        readonly property color transparent: 'transparent'
        readonly property color paleGray: '#E5E5E6'
        readonly property color lightGray: '#D5D6D7'
        readonly property color mutedGray: '#97999B'
        readonly property color charcoalGray: '#545556'
        readonly property color slateGray: '#212223'
        readonly property color onyxBlack: '#101010'
        readonly property color midnightBlack: '#000000'
        readonly property color goldenApricot: '#2757FF'
        readonly property color burntOrange: '#002BC4'
        readonly property color mutedBrown: '#002BC4'
        readonly property color richBrown: '#2757FF'
        readonly property color deepBrown: '#2757FF'
        readonly property color vibrantRed: '#CC0102'
        readonly property color darkCharcoal: '#1A1A1B'
        readonly property color sheerWhite: Qt.rgba(1, 1, 1, 0.12)
        readonly property color translucentWhite: Qt.rgba(1, 1, 1, 0.08)
        readonly property color barelyTranslucentWhite: Qt.rgba(1, 1, 1, 0.05)
        readonly property color translucentMidnightBlack: Qt.rgba(0, 0, 0, 0.8)
        readonly property color softGoldenApricot: Qt.rgba(39/255, 87/255, 255/255, 0.3)
        readonly property color mistyGray: Qt.rgba(215/255, 216/255, 219/255, 0.8)
        readonly property color cloudyGray: Qt.rgba(215/255, 216/255, 219/255, 0.65)
        readonly property color pearlGray: '#EAEAEC'
        readonly property color translucentRichBrown: Qt.rgba(39/255, 87/255, 255/255, 0.26)
        readonly property color translucentSlateGray: Qt.rgba(85/255, 86/255, 92/255, 0.13)
        readonly property color translucentOnyxBlack: Qt.rgba(28/255, 29/255, 33/255, 0.13)
    }

    property QtObject color: regularColorPalette
}
