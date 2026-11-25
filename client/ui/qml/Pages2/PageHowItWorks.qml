import QtQuick
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

import PageEnum 1.0
import Style 1.0

import "../Controls2"
import "../Controls2/TextTypes"

// Explainer for the routing model: the user picks the entry server, the exit is
// chosen by the service. No data is read here — the copy is the whole page.
// Source of truth for all three languages: frkn-docs/user-routing-scheme.md
PageType {
    id: root

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin
    }

    FlickableType {
        id: fl

        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        anchors.bottomMargin: SettingsController.safeAreaBottomMargin
        anchors.left: parent.left
        anchors.right: parent.right

        contentHeight: content.implicitHeight

        ColumnLayout {
            id: content

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 8
                Layout.bottomMargin: 20

                headerText: qsTr("Servers, protocols, routes")
                descriptionText: qsTr("You pick where to connect and with which protocol. Where the traffic actually goes is decided by the service — the rules are further down.")
            }

            // ---- What the server list is -------------------------------------
            // Section headings are small caps like on the site: the source text
            // stays normal case, the font does the uppercasing, so RU/UK
            // translations keep their usual spelling.
            Text {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 10

                text: qsTr("The server list")
                color: DopamineStyle.color.mutedGray
                font.pixelSize: 11
                font.weight: 700
                font.letterSpacing: 1.6
                font.capitalization: Font.AllUppercase
            }

            Repeater {
                model: root.listItems

                delegate: RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.bottomMargin: 10

                    spacing: 10

                    Text {
                        Layout.alignment: Qt.AlignTop

                        text: "·"
                        color: DopamineStyle.color.goldenApricot
                        font.pixelSize: 16
                        font.weight: 700
                    }

                    LabelTextType {
                        Layout.fillWidth: true

                        text: modelData
                        color: DopamineStyle.color.mutedGray
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ---- Protocols ---------------------------------------------------
            Text {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 10
                Layout.bottomMargin: 4

                text: qsTr("Protocols")
                color: DopamineStyle.color.mutedGray
                font.pixelSize: 11
                font.weight: 700
                font.letterSpacing: 1.6
                font.capitalization: Font.AllUppercase
            }

            Repeater {
                model: root.protocolItems

                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.topMargin: 12
                        Layout.bottomMargin: 12

                        spacing: 12

                        Item {
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 22

                            Image {
                                anchors.centerIn: parent

                                source: modelData.icon
                                sourceSize.width: 22
                                sourceSize.height: 22
                                fillMode: Image.PreserveAspectFit

                                // monochrome marks: the app tints icons through a
                                // ColorOverlay, same as the rest of the interface
                                layer.enabled: true
                                layer.effect: ColorOverlay {
                                    color: DopamineStyle.color.paleGray
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                Layout.fillWidth: true

                                // Protocol names are not translated (AGENTS.md)
                                text: modelData.title
                                color: DopamineStyle.color.paleGray
                                font.pixelSize: 15
                                font.weight: 600
                                font.family: "IBM Plex Mono"
                                wrapMode: Text.Wrap
                            }

                            LabelTextType {
                                Layout.fillWidth: true

                                text: modelData.description
                                color: DopamineStyle.color.mutedGray
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    DividerType {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                    }
                }
            }

            CaptionTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 12

                text: qsTr("Switch the protocol with the pill in the server row — several pills mean the server speaks more than one. The choice applies at once and the tunnel re-dials.")
                color: DopamineStyle.color.mutedGray
                wrapMode: Text.Wrap
            }

            // Same diagram as frkn-docs/user-routing-scheme.svg (device → entry →
            // routing), stacked vertically because the window is phone-width and
            // drawn in QML so the labels follow the interface language. The exits
            // themselves are the rows below, not small text inside the diagram.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 24
                Layout.preferredHeight: diagram.implicitHeight + 28

                radius: 16
                color: DopamineStyle.color.onyxBlack
                border.color: DopamineStyle.color.slateGray
                border.width: 1

                ColumnLayout {
                    id: diagram

                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.topMargin: 14
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 0

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true

                            text: qsTr("Your device")
                            color: DopamineStyle.color.paleGray
                            font.pixelSize: 14
                            font.weight: 600
                            font.family: "IBM Plex Mono"
                            horizontalAlignment: Text.AlignHCenter
                        }

                        CaptionTextType {
                            Layout.fillWidth: true

                            text: qsTr("Dopamine app")
                            color: DopamineStyle.color.mutedGray
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Item {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20

                        Image {
                            id: arrowToEntry

                            anchors.fill: parent
                            source: "qrc:/images/controls/chevron-down.svg"
                            sourceSize: Qt.size(20, 20)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true

                            text: qsTr("Entry")
                            color: DopamineStyle.color.paleGray
                            font.pixelSize: 14
                            font.weight: 600
                            font.family: "IBM Plex Mono"
                            horizontalAlignment: Text.AlignHCenter
                        }

                        CaptionTextType {
                            Layout.fillWidth: true

                            text: qsTr("the server that works best for you")
                            color: DopamineStyle.color.mutedGray
                            horizontalAlignment: Text.AlignHCenter
                        }

                        CaptionTextType {
                            Layout.fillWidth: true
                            Layout.topMargin: 2

                            text: qsTr("you choose it")
                            color: DopamineStyle.color.goldenApricot
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Item {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        Layout.bottomMargin: 4
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20

                        Image {
                            id: arrowToRouting

                            anchors.fill: parent
                            source: "qrc:/images/controls/chevron-down.svg"
                            sourceSize: Qt.size(20, 20)
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true

                            text: qsTr("Routing")
                            color: DopamineStyle.color.paleGray
                            font.pixelSize: 14
                            font.weight: 600
                            font.family: "IBM Plex Mono"
                            horizontalAlignment: Text.AlignHCenter
                        }

                        CaptionTextType {
                            Layout.fillWidth: true

                            text: qsTr("we find the best exit")
                            color: DopamineStyle.color.mutedGray
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
            }

            Repeater {
                model: root.routingItems

                delegate: ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.topMargin: 12
                        Layout.bottomMargin: 12

                        spacing: 12

                        Item {
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 20

                            Image {
                                anchors.centerIn: parent

                                source: modelData.icon
                                sourceSize.width: 18
                                sourceSize.height: 18
                                fillMode: Image.PreserveAspectFit

                                layer.enabled: true
                                layer.effect: ColorOverlay {
                                    color: DopamineStyle.color.mutedGray
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                Layout.fillWidth: true

                                text: modelData.title
                                color: DopamineStyle.color.paleGray
                                font.pixelSize: 15
                                font.weight: 500
                                font.family: "IBM Plex Mono"
                                font.letterSpacing: 0.02
                                wrapMode: Text.Wrap
                            }

                            LabelTextType {
                                Layout.fillWidth: true

                                text: "→ " + modelData.route
                                color: DopamineStyle.color.mutedGray
                            }
                        }
                    }

                    DividerType {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                    }
                }
            }

            // The honest footnote: a missing exit never gets replaced silently.
            // It is a promise about bans, not a detail — the warning frame and the
            // triangle keep it from reading as fine print.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 16
                Layout.preferredHeight: warningRow.implicitHeight + 24

                radius: 12
                color: DopamineStyle.color.sheerWhite
                border.color: DopamineStyle.color.goldenApricot
                border.width: 1

                RowLayout {
                    id: warningRow

                    anchors.fill: parent
                    anchors.margins: 12

                    spacing: 10

                    Image {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20

                        source: "qrc:/images/controls/alert-triangle.svg"
                        sourceSize.width: 20
                        sourceSize.height: 20
                        fillMode: Image.PreserveAspectFit

                        layer.enabled: true
                        layer.effect: ColorOverlay {
                            color: DopamineStyle.color.goldenApricot
                        }
                    }

                    LabelTextType {
                        Layout.fillWidth: true

                        text: qsTr("If an exit is down, the service will not open — we never silently switch you to another country, so you don't get banned.")
                        color: DopamineStyle.color.paleGray
                        wrapMode: Text.Wrap
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                Layout.topMargin: 24

                spacing: 12

                BasicButtonType {
                    id: gotItButton

                    Layout.fillWidth: true
                    implicitHeight: 48

                    defaultColor: DopamineStyle.color.transparent
                    hoveredColor: DopamineStyle.color.translucentWhite
                    pressedColor: DopamineStyle.color.sheerWhite
                    borderColor: DopamineStyle.color.slateGray
                    borderWidth: 1
                    textColor: DopamineStyle.color.paleGray

                    text: qsTr("Got it")

                    clickedFunc: function() {
                        PageController.closePage()
                    }
                }

                BasicButtonType {
                    id: chooseServerButton

                    Layout.fillWidth: true
                    implicitHeight: 48

                    text: qsTr("Choose a server")

                    clickedFunc: function() {
                        PageController.goToPage(PageEnum.PageSettingsServersList)
                    }
                }
            }
        }
    }

    // Row order follows the exits in frkn-docs/user-routing-scheme.svg
    // (RU → US → separate → direct), so the list reads like the scheme.
    property list<QtObject> routingItems: [
        countrySites,
        aiServices,
        governmentSites,
        chineseAi,
        everythingElse
    ]

    // What the screen with the list of servers is, in three lines.
    property list<string> listItems: [
        qsTr("These are entry points: the server your app connects to. A tap on a row connects, another tap moves the tunnel."),
        qsTr("The ping next to a name shows how fast that server answers. “Auto-select” takes the best one for you."),
        qsTr("The buttons above reload the list, filter it by subscription and add a server by hand.")
    ]

    // Protocol names stay as they are in the interface: AmneziaWG is a product
    // name, not a word to translate (AGENTS.md).
    property list<QtObject> protocolItems: [
        protoAwg,
        protoWg,
        protoVless,
        protoElephant,
        protoHysteria
    ]

    QtObject {
        id: protoAwg

        readonly property string icon: "qrc:/images/controls/shield.svg"
        readonly property string title: "AmneziaWG"
        readonly property string description: qsTr("WireGuard with obfuscation: the packets do not look like a VPN, so DPI does not cut the tunnel. The default choice.")
    }

    QtObject {
        id: protoWg

        readonly property string icon: "qrc:/images/protocols/wireguard.svg"
        readonly property string title: "WireGuard"
        readonly property string description: qsTr("Plain WireGuard: the fastest of all, but easy to detect — your provider may throttle it.")
    }

    QtObject {
        id: protoVless

        readonly property string icon: "qrc:/images/protocols/vless.svg"
        readonly property string title: "VLESS"
        readonly property string description: qsTr("Xray over HTTPS: the session looks like ordinary web traffic. Use it where WireGuard is blocked.")
    }

    QtObject {
        id: protoElephant

        readonly property string icon: "qrc:/images/protocols/belyslon.svg"
        readonly property string title: qsTr("White Elephant") + " · VLESS CDN"
        readonly property string description: qsTr("VLESS through a CDN: the traffic rides along as regular requests to a popular site. Hardest to detect, but the slowest of the set.")
    }

    QtObject {
        id: protoHysteria

        readonly property string icon: "qrc:/images/controls/wind.svg"
        readonly property string title: "Hysteria2"
        readonly property string description: qsTr("QUIC over UDP: keeps its speed on lossy links and high ping. Needs UDP not to be filtered.")
    }

    QtObject {
        id: countrySites

        readonly property string icon: "qrc:/images/controls/credit-card.svg"
        readonly property string title: qsTr("Sites and services of your country, exchanges")
        readonly property string route: qsTr("via the RU exit")
    }

    QtObject {
        id: governmentSites

        readonly property string icon: "qrc:/images/controls/file-text.svg"
        readonly property string title: qsTr("Government sites (Gosuslugi, Pochta, mos.ru, tax office)")
        readonly property string route: qsTr("via a separate exit — they reject our regular addresses")
    }

    QtObject {
        id: aiServices

        readonly property string icon: "qrc:/images/controls/cpu.svg"
        readonly property string title: qsTr("AI services (Claude, ChatGPT, Gemini)")
        readonly property string route: qsTr("via the US")
    }

    QtObject {
        id: chineseAi

        readonly property string icon: "qrc:/images/controls/message-circle.svg"
        readonly property string title: qsTr("Chinese AI (DeepSeek, Qwen, Kimi)")
        readonly property string route: qsTr("straight from the server, faster and more stable")
    }

    QtObject {
        id: everythingElse

        readonly property string icon: "qrc:/images/controls/lock.svg"
        readonly property string title: qsTr("Everything else")
        readonly property string route: qsTr("straight from the server, no extra hops")
    }
}
