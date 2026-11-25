import QtQuick
import QtQuick.Window

import Style 1.0

import "../Controls2/TextTypes"

// "Servers loaded ^_^" fly-by shown after the peer-pool cards of a FRKN
// subscription are imported: the home-screen pterodactyl takes off upward
// carrying a banner. The flight lives in the upper half of the screen so it
// reads as an announcement and not as a footer. Purely decorative — it never
// accepts input and never blocks touches under it.
Item {
    id: root

    anchors.fill: parent
    z: 150

    // decorative overlay: no focus, no input, no touch blocking
    enabled: false
    focus: false

    // kept for the caller's signature (PageController::showServersAddedBanner);
    // the banner text no longer prints a number
    property int count: 0
    property bool running: flyBy.running

    readonly property int sideMargin: 16
    readonly property int bannerMaxWidth: Math.max(120, width - 2 * sideMargin)

    // Flight envelope. The flyer's bottom edge starts a little above the
    // vertical middle and the lift carries it into the top third, so the
    // mascot is always in the upper part of the window instead of the footer.
    readonly property int flightBottom: Math.round(root.height * 0.54)

    // clamped so the flight stays on screen on short windows (landscape phones)
    readonly property int riseDistance: Math.round(Math.max(130, Math.min(200, height * 0.18)))

    // The import reports once per granted shard (`cardsReady` fires per env) and
    // the list reload reports once more when its parts drain. A restart while
    // the mascot was airborne threw it back to the floor, so one import looked
    // like three jumps instead of a single take-off. One flight per burst: a
    // call that arrives in flight, or right after landing, folds into it.
    readonly property int reflyCooldownMs: 900
    property real landedAt: 0

    function show(n) {
        root.count = n
        if (flyBy.running || Date.now() - landedAt < reflyCooldownMs) {
            return
        }
        flyBy.restart()
    }

    Connections {
        target: flyBy

        function onRunningChanged() {
            if (!flyBy.running) {
                root.landedAt = Date.now()
            }
        }
    }

    Item {
        id: flyer

        width: banner.width
        height: banner.y + banner.height

        x: (root.width - width) / 2 + sway
        // never let the lift push the mascot off the top edge of a short window
        y: Math.max(8, root.flightBottom - height - lift)

        opacity: 0
        visible: opacity > 0
        scale: 0.9
        transformOrigin: Item.Center

        property real lift: 0
        property real sway: 0

        Image {
            id: ptero

            anchors.horizontalCenter: parent.horizontalCenter
            y: 0

            source: "qrc:/images/pterodactyl.png"
            sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
            asynchronous: true
            cache: true

            width: 88
            height: 88
            fillMode: Image.PreserveAspectFit

            rotation: -6
        }

        Rectangle {
            id: strap

            anchors.horizontalCenter: parent.horizontalCenter
            y: ptero.y + ptero.height - 4

            width: 2
            height: 12

            color: DopamineStyle.color.mutedGray
            opacity: 0.6
        }

        Rectangle {
            id: banner

            anchors.horizontalCenter: parent.horizontalCenter
            y: strap.y + strap.height

            width: Math.min(metrics.advanceWidth + 32, root.bannerMaxWidth)
            height: bannerLabel.implicitHeight + 16

            radius: 8
            color: DopamineStyle.color.onyxBlack

            TextMetrics {
                id: metrics

                font: bannerLabel.font
                text: bannerLabel.text
            }

            CaptionTextType {
                id: bannerLabel

                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.topMargin: 8
                anchors.bottomMargin: 8

                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere

                // larger than CaptionTextType's 13 px — the banner is the whole
                // point of the animation; lineHeight raised with it so the
                // fixed-height lines do not clip the glyphs
                font.pixelSize: 20
                font.weight: 500
                lineHeight: 24 + LanguageModel.getLineHeightAppend()

                color: DopamineStyle.color.paleGray
                text: qsTr("Servers loaded ^_^")
            }
        }
    }

    // 1.87 s in total: fade in, rise 130-200 px with a gentle sway and a
    // slight grow, then fade out.
    SequentialAnimation {
        id: flyBy

        PropertyAction { target: flyer; property: "lift"; value: 0 }
        PropertyAction { target: flyer; property: "sway"; value: 0 }
        PropertyAction { target: flyer; property: "opacity"; value: 0 }
        PropertyAction { target: flyer; property: "scale"; value: 0.9 }

        ParallelAnimation {
            SequentialAnimation {
                NumberAnimation { target: flyer; property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.OutQuad }
                PauseAnimation { duration: 1350 }
                NumberAnimation { target: flyer; property: "opacity"; to: 0; duration: 320; easing.type: Easing.InQuad }
            }

            NumberAnimation {
                target: flyer
                property: "lift"
                from: 0
                to: root.riseDistance
                duration: 1870
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                target: flyer
                property: "scale"
                from: 0.9
                to: 1.05
                duration: 1870
                easing.type: Easing.OutQuad
            }

            SequentialAnimation {
                NumberAnimation { target: flyer; property: "sway"; from: 0; to: 9; duration: 480; easing.type: Easing.InOutSine }
                NumberAnimation { target: flyer; property: "sway"; to: -8; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { target: flyer; property: "sway"; to: 5; duration: 470; easing.type: Easing.InOutSine }
                NumberAnimation { target: flyer; property: "sway"; to: 0; duration: 400; easing.type: Easing.InOutSine }
            }
        }

        PropertyAction { target: flyer; property: "opacity"; value: 0 }
    }
}
