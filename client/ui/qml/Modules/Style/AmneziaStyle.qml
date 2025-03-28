pragma Singleton

import QtQuick

QtObject {
    property QtObject regularColorPalette: QtObject {
        readonly property color transparent: 'transparent'
        readonly property color paleGray: '#121314'  // Инвертирован
        readonly property color lightGray: '#2A2B2C'  // Инвертирован
        readonly property color mutedGray: '#686A6D'  // Инвертирован
        readonly property color charcoalGray: '#ABACAD'  // Инвертирован
        readonly property color slateGray: '#DEDFE0'  // Инвертирован
        readonly property color onyxBlack: '#EFEFEF'  // Инвертирован
        readonly property color midnightBlack: '#FFFFFF'  // Инвертирован
        readonly property color goldenApricot: '#2757FF'  // Заменен на синий
        readonly property color burntOrange: '#002BC4'  // Заменен на синий
        readonly property color mutedBrown: '#002BC4'  // Заменен на синий
        readonly property color richBrown: '#2757FF'  // Заменен на синий
        readonly property color deepBrown: '#2757FF'  // Заменен на синий
        readonly property color vibrantRed: '#9B0104'  // Инвертирован
        readonly property color darkCharcoal: '#E5E5E6'  // Инвертирован
        readonly property color sheerWhite: Qt.rgba(0, 0, 0, 0.12)
        readonly property color translucentWhite: Qt.rgba(0, 0, 0, 0.08)
        readonly property color barelyTranslucentWhite: Qt.rgba(0, 0, 0, 0.05)
    }

    property QtObject darkColorPalette: QtObject {
        readonly property color transparent: 'transparent'
        readonly property color paleGray: '#E5E5E6'  // Инвертирован
        readonly property color lightGray: '#D5D6D7'  // Инвертирован
        readonly property color mutedGray: '#97999B'  // Инвертирован
        readonly property color charcoalGray: '#545556'  // Инвертирован
        readonly property color slateGray: '#212223'  // Инвертирован
        readonly property color onyxBlack: '#101010'  // Инвертирован
        readonly property color midnightBlack: '#000000'  // Инвертирован
        readonly property color goldenApricot: '#2757FF'  // Синий, не трогать
        readonly property color burntOrange: '#002BC4'  // Синий, не трогать
        readonly property color mutedBrown: '#002BC4'  // Синий, не трогать
        readonly property color richBrown: '#2757FF'  // Синий, не трогать
        readonly property color deepBrown: '#2757FF'  // Синий, не трогать
        readonly property color vibrantRed: '#CC0102'  // Инвертирован
        readonly property color darkCharcoal: '#1A1A1B'  // Инвертирован
        readonly property color sheerWhite: Qt.rgba(1, 1, 1, 0.12)  // Инвертирован
        readonly property color translucentWhite: Qt.rgba(1, 1, 1, 0.08)  // Инвертирован
        readonly property color barelyTranslucentWhite: Qt.rgba(1, 1, 1, 0.05)  // Инвертирован
    }


    property QtObject color: regularColorPalette

    property QtObject frknColorPalette: QtObject {
        readonly property color mainBlue: '#2757FF'
        readonly property color clickBlue: '#002BC4'
        readonly property color white: '#FFFFFF'
        readonly property color black: '#121314'
        readonly property color grey: '#686A6D'
        readonly property color lightGrey: '#ABACAD'
        readonly property color errorRed: '#9B0104'
        readonly property color backgroundGrey: '#EFEFEF'
    }
}
