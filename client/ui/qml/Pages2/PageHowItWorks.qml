import QtQuick
import QtQuick.Layouts

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

                headerText: qsTr("You pick the entry. We find the exit.")
                descriptionText: qsTr("Connect to whichever server works best for you. Where the traffic goes is decided for you, and does not depend on that choice.")
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

                            text: qsTr("we pick the best exit")
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

                        Text {
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 26

                            // the icon is the one from the approved copy; emoji come
                            // from the system fallback font, the text below stays
                            // in IBM Plex Mono
                            text: modelData.icon
                            font.pixelSize: 18
                            horizontalAlignment: Text.AlignHCenter
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
            CaptionTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 16

                text: qsTr("If an exit is down, the service will not open — we never silently switch you to another country, so you don't get banned.")
                color: DopamineStyle.color.mutedGray
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
    // (RU → US → KZ → separate → direct), so the list reads like the scheme.
    property list<QtObject> routingItems: [
        countrySites,
        aiServices,
        youtube,
        governmentSites,
        chineseAi,
        everythingElse
    ]

    QtObject {
        id: countrySites

        readonly property string icon: "🏦"
        readonly property string title: qsTr("Sites and services of your country, exchanges")
        readonly property string route: qsTr("via the RU exit")
    }

    QtObject {
        id: governmentSites

        readonly property string icon: "🏛️"
        readonly property string title: qsTr("Government sites (Gosuslugi, Pochta, mos.ru, tax office)")
        readonly property string route: qsTr("via a separate exit — they reject our regular addresses")
    }

    QtObject {
        id: aiServices

        readonly property string icon: "🤖"
        readonly property string title: qsTr("AI services (Claude, ChatGPT, Gemini)")
        readonly property string route: qsTr("via the US")
    }

    QtObject {
        id: youtube

        readonly property string icon: "▶️"
        readonly property string title: qsTr("YouTube")
        readonly property string route: qsTr("via Kazakhstan, which is why there are no built-in ads")
    }

    QtObject {
        id: chineseAi

        readonly property string icon: "🌐"
        readonly property string title: qsTr("Chinese AI (DeepSeek, Qwen, Kimi)")
        readonly property string route: qsTr("straight from the server, faster and more stable")
    }

    QtObject {
        id: everythingElse

        readonly property string icon: "🔒"
        readonly property string title: qsTr("Everything else")
        readonly property string route: qsTr("straight from the server, no extra hops")
    }
}
