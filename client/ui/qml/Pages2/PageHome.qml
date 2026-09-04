import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

import SortFilterProxyModel 0.2

import PageEnum 1.0
import ProtocolEnum 1.0
import ContainerProps 1.0
import ContainersModelFilters 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    ImageButtonType {
        id: settingsButton
        objectName: "settingsButton"

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 12 + SettingsController.safeAreaTopMargin
        anchors.rightMargin: 12

        z: 10

        implicitWidth: 48
        implicitHeight: 48

        image: "qrc:/images/controls/settings.svg"
        imageColor: AmneziaStyle.color.paleGray

        onClicked: PageController.goToPage(PageEnum.PageSettings)
    }

    Connections {
        target: Qt.application

        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) {
                if (homeSplitTunnelingDrawer.isOpened) {
                    homeSplitTunnelingDrawer.closeTriggered()
                }
            }
        }
    }

    Item {
        objectName: "homeColumnItem"

        anchors.fill: parent

        ColumnLayout {
            objectName: "homeColumnLayout"

            anchors.fill: parent
            anchors.topMargin: 12 + SettingsController.safeAreaTopMargin
            anchors.bottomMargin: 16

            BasicButtonType {
                id: loggingButton
                objectName: "loggingButton"

                property bool isLoggingEnabled: SettingsController.isLoggingEnabled

                Layout.alignment: Qt.AlignHCenter

                implicitHeight: 36

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                disabledColor: AmneziaStyle.color.mutedGray
                textColor: AmneziaStyle.color.mutedGray
                borderWidth: 0

                visible: isLoggingEnabled ? true : false
                text: qsTr("Diagnostic Mode Enabled")

                Keys.onEnterPressed: this.clicked()
                Keys.onReturnPressed: this.clicked()

                onClicked: {
                    PageController.goToPage(PageEnum.PageSettingsLogging)
                }
            }

            BasicButtonType {
                id: devGatewayButton
                objectName: "devGatewayButton"

                property bool isDevGatewayEnabled: SettingsController.isDevGatewayEnv

                Layout.alignment: Qt.AlignHCenter

                implicitHeight: 36

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                disabledColor: AmneziaStyle.color.mutedGray
                textColor: AmneziaStyle.color.mutedGray
                borderWidth: 0

                visible: SettingsController.isDevModeEnabled && isDevGatewayEnabled
                text: qsTr("Dev gateway enabled")

                Keys.onEnterPressed: this.clicked()
                Keys.onReturnPressed: this.clicked()

                onClicked: {
                    PageController.goToPage(PageEnum.PageDevMenu)
                }
            }

            ConnectButton {
                id: connectButton
                objectName: "connectButton"

                Layout.fillHeight: true
                Layout.alignment: Qt.AlignCenter
            }

            Rectangle {
                id: serverCard
                objectName: "serverCard"

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8

                implicitHeight: 88
                radius: 20

                color: serverCardMouse.containsPress ? AmneziaStyle.color.sheerWhite
                                                     : AmneziaStyle.color.translucentWhite

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 12

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        ListItemTitleType {
                            Layout.fillWidth: true

                            maximumLineCount: 1
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                            font.pixelSize: 22
                            font.weight: 600

                            text: SettingsController.autoServerSelection && !ConnectionController.isConnected
                                  ? qsTr("Auto-select")
                                  : ServersModel.defaultServerName
                        }

                        CaptionTextType {
                            Layout.fillWidth: true

                            visible: text !== ""
                            color: AmneziaStyle.color.mutedGray
                            font.pixelSize: 14
                            maximumLineCount: 1
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap

                            text: ServersModel.defaultServerProtocolName
                        }
                    }

                }

                MouseArea {
                    id: serverCardMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor

                    onClicked: PageController.goToPage(PageEnum.PageSettingsServersList)
                }

                ImageButtonType {
                    id: serverCardInfoButton
                    objectName: "serverCardInfoButton"

                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter

                    implicitWidth: 56
                    implicitHeight: 56

                    image: "qrc:/images/controls/info.svg"
                    imageColor: AmneziaStyle.color.mutedGray

                    onClicked: {
                        ServersModel.processedIndex = ServersModel.defaultIndex

                        if (ServersModel.getProcessedServerData("isServerFromGatewayApi")) {
                            if (ServersModel.getProcessedServerData("isCountrySelectionAvailable")) {
                                PageController.goToPage(PageEnum.PageSettingsApiAvailableCountries)
                            } else {
                                PageController.showBusyIndicator(true)
                                let result = ApiSettingsController.getAccountInfo(false)
                                PageController.showBusyIndicator(false)
                                if (!result) {
                                    return
                                }

                                PageController.goToPage(PageEnum.PageSettingsApiServerInfo)
                            }
                        } else {
                            PageController.goToPage(PageEnum.PageSettingsServerInfo)
                        }
                    }
                }
            }

            BasicButtonType {
                id: splitTunnelingButton
                objectName: "splitTunnelingButton"

                Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
                leftPadding: 16
                rightPadding: 16

                implicitHeight: 36

                defaultColor: AmneziaStyle.color.transparent
                hoveredColor: AmneziaStyle.color.translucentWhite
                pressedColor: AmneziaStyle.color.sheerWhite
                disabledColor: AmneziaStyle.color.mutedGray
                textColor: AmneziaStyle.color.mutedGray
                borderWidth: 0

                buttonTextLabel.lineHeight: 20
                buttonTextLabel.font.pixelSize: 14
                buttonTextLabel.font.weight: 500

                property bool isSplitTunnelingEnabled: SitesModel.isTunnelingEnabled || AppSplitTunnelingModel.isTunnelingEnabled ||
                                                       ServersModel.isDefaultServerDefaultContainerHasSplitTunneling

                text: isSplitTunnelingEnabled ? qsTr("Split tunneling enabled") : qsTr("Split tunneling disabled")

                leftImageSource: isSplitTunnelingEnabled ? "qrc:/images/controls/split-tunneling.svg" : ""
                leftImageColor: ""
                rightImageSource: "qrc:/images/controls/chevron-down.svg"

                Keys.onEnterPressed: this.clicked()
                Keys.onReturnPressed: this.clicked()

                onClicked: {
                    homeSplitTunnelingDrawer.openTriggered()
                }

                HomeSplitTunnelingDrawer {
                    id: homeSplitTunnelingDrawer
                    objectName: "homeSplitTunnelingDrawer"

                    parent: root
                }
            }

            AdLabel {
                id: adLabel

                Layout.fillWidth: true
                Layout.preferredHeight: adLabel.contentHeight
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 22
            }

        }
    }

    property bool wasConnected: ConnectionController.isConnected

    Component.onCompleted: {
        connectButton.birdPerched = ConnectionController.isConnected
    }

    function startPterodactylFlight() {
        const center = connectButton.mapToItem(root, connectButton.width / 2, connectButton.height / 2)
        pterodactyl.perchX = center.x - pterodactyl.width / 2
        pterodactyl.perchY = center.y - pterodactyl.height / 2
        pterodactylFlight.restart()
    }

    Connections {
        target: ConnectionController

        function onConnectionStateChanged() {
            const connected = ConnectionController.isConnected
            if (connected && !ConnectionController.isConnectionInProgress && !connectButton.birdPerched && !pterodactylFlight.running) {
                // settled into a real connection — fly (covers auto-select, where the
                // Connected state arrives while the traffic check is still running)
                root.startPterodactylFlight()
            } else if (!connected && root.wasConnected) {
                connectButton.birdPerched = false
                pterodactylFlight.stop()
                pterodactyl.opacity = 0
            }
            root.wasConnected = connected
        }
    }

    Image {
        id: pterodactyl

        property real perchX: 0
        property real perchY: 0

        source: "qrc:/images/pterodactyl.png"
        width: 96
        height: 96
        fillMode: Image.PreserveAspectFit

        x: (root.width - width) / 2
        y: root.height
        z: 100

        opacity: 0
        visible: opacity > 0

        SequentialAnimation {
            id: pterodactylFlight

            PropertyAction { target: pterodactyl; property: "x"; value: (root.width - pterodactyl.width) / 2 }
            PropertyAction { target: pterodactyl; property: "y"; value: root.height * 0.55 }
            PropertyAction { target: pterodactyl; property: "scale"; value: 1.3 }
            PropertyAction { target: pterodactyl; property: "rotation"; value: -8 }

            ParallelAnimation {
                NumberAnimation {
                    target: pterodactyl
                    property: "x"
                    to: pterodactyl.perchX
                    duration: 1300
                    easing.type: Easing.InOutQuad
                }
                NumberAnimation {
                    target: pterodactyl
                    property: "y"
                    to: pterodactyl.perchY
                    duration: 1300
                    easing.type: Easing.InQuad
                }
                NumberAnimation {
                    target: pterodactyl
                    property: "scale"
                    to: 1.0
                    duration: 1300
                    easing.type: Easing.InOutQuad
                }

                SequentialAnimation {
                    NumberAnimation { target: pterodactyl; property: "opacity"; from: 0; to: 1; duration: 200 }
                    PauseAnimation { duration: 900 }
                    NumberAnimation { target: pterodactyl; property: "opacity"; to: 0.22; duration: 200 }
                }

                SequentialAnimation {
                    NumberAnimation { target: pterodactyl; property: "rotation"; from: -8; to: 10; duration: 350; easing.type: Easing.InOutSine }
                    NumberAnimation { target: pterodactyl; property: "rotation"; to: -6; duration: 350; easing.type: Easing.InOutSine }
                    NumberAnimation { target: pterodactyl; property: "rotation"; to: 0; duration: 300; easing.type: Easing.InOutSine }
                }
            }

            PropertyAction { target: pterodactyl; property: "opacity"; value: 0 }

            onStopped: {
                if (ConnectionController.isConnected) {
                    connectButton.birdPerched = true
                }
                pterodactyl.scale = 1
            }
        }

        SequentialAnimation {
            id: pterodactylSideTrip

            property real direction: 1

            PropertyAction { target: pterodactyl; property: "x"; value: pterodactyl.perchX }
            PropertyAction { target: pterodactyl; property: "y"; value: pterodactyl.perchY }
            PropertyAction { target: pterodactyl; property: "scale"; value: 1.0 }
            PropertyAction { target: pterodactyl; property: "rotation"; value: 0 }
            PropertyAction { target: pterodactyl; property: "opacity"; value: 1 }

            ParallelAnimation {
                // loop-the-loop: out to a random side, arc over the top to the other
                // side, then dive back into the connect button
                PathAnimation {
                    target: pterodactyl
                    duration: 1600
                    easing.type: Easing.InOutSine
                    anchorPoint: Qt.point(pterodactyl.width / 2, pterodactyl.height / 2)

                    path: Path {
                        id: tripPath

                        property real centerX: pterodactyl.perchX + pterodactyl.width / 2
                        property real centerY: pterodactyl.perchY + pterodactyl.height / 2
                        property real dir: pterodactylSideTrip.direction

                        startX: centerX
                        startY: centerY

                        PathQuad {
                            x: tripPath.centerX + tripPath.dir * root.width * 0.45
                            y: tripPath.centerY - root.height * 0.16
                            controlX: tripPath.centerX + tripPath.dir * root.width * 0.3
                            controlY: tripPath.centerY - root.height * 0.02
                        }

                        PathCubic {
                            x: tripPath.centerX - tripPath.dir * root.width * 0.2
                            y: tripPath.centerY - root.height * 0.34
                            control1X: tripPath.centerX + tripPath.dir * root.width * 0.5
                            control1Y: tripPath.centerY - root.height * 0.4
                            control2X: tripPath.centerX - tripPath.dir * root.width * 0.12
                            control2Y: tripPath.centerY - root.height * 0.45
                        }

                        PathQuad {
                            x: tripPath.centerX
                            y: tripPath.centerY
                            controlX: tripPath.centerX - tripPath.dir * root.width * 0.25
                            controlY: tripPath.centerY - root.height * 0.12
                        }
                    }
                }

                SequentialAnimation {
                    NumberAnimation { target: pterodactyl; property: "rotation"; from: 0; to: pterodactylSideTrip.direction * 18; duration: 500; easing.type: Easing.InOutSine }
                    NumberAnimation { target: pterodactyl; property: "rotation"; to: -pterodactylSideTrip.direction * 10; duration: 600; easing.type: Easing.InOutSine }
                    NumberAnimation { target: pterodactyl; property: "rotation"; to: 0; duration: 500; easing.type: Easing.InOutSine }
                }
            }

            PropertyAction { target: pterodactyl; property: "opacity"; value: 0 }

            onStarted: {
                connectButton.birdPerched = false
            }

            onStopped: {
                if (ConnectionController.isConnected) {
                    connectButton.birdPerched = true
                }
            }
        }
    }

    Connections {
        target: PageController

        function onShakeDetected() {
            // defer out of the signal delivery path
            Qt.callLater(root.startPterodactylSideTrip)
        }
    }

    function startPterodactylSideTrip() {
        if (!ConnectionController.isConnected || pterodactylFlight.running || pterodactylSideTrip.running) {
            return
        }
        const center = connectButton.mapToItem(root, connectButton.width / 2, connectButton.height / 2)
        pterodactyl.perchX = center.x - pterodactyl.width / 2
        pterodactyl.perchY = center.y - pterodactyl.height / 2
        pterodactylSideTrip.direction = Math.random() < 0.5 ? -1 : 1
        pterodactylSideTrip.restart()
    }
}
