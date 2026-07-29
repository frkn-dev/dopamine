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

    Connections {
        target: Qt.application

        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) {
                if (drawer.isOpened) {
                    drawer.closeTriggered()
                }
                if (homeSplitTunnelingDrawer.isOpened) {
                    homeSplitTunnelingDrawer.closeTriggered()
                }
            }
        }
    }

    Connections {
        objectName: "pageControllerConnections"

        target: PageController

        function onRestorePageHomeState(isContainerInstalled) {
            drawer.openTriggered()
            if (isContainerInstalled) {
                containersDropDown.rootButtonClickedFunction()
            }
        }
    }


    Item {
        objectName: "homeColumnItem"

        anchors.fill: parent
        anchors.bottomMargin: drawer.collapsedHeight

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
                text: qsTr("Logging enabled")

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

    DrawerType2 {
        id: drawer
        objectName: "drawerProtocol"

        anchors.fill: parent

        collapsedStateContent: Item {
            objectName: "ProtocolDrawerCollapsedContent"

            implicitHeight: Qt.platform.os !== "ios" ? root.height * 0.9 : screen.height * 0.77
            Component.onCompleted: {
                drawer.expandedHeight = implicitHeight
            }

            ColumnLayout {
                id: collapsed
                objectName: "collapsedColumnLayout"

                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0

                Component.onCompleted: {
                    drawer.collapsedHeight = collapsed.implicitHeight
                }

                DividerType {
                    Layout.topMargin: 10
                    Layout.fillWidth: false
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 2
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                }

                RowLayout {
                    objectName: "rowLayout"

                    Layout.topMargin: drawer.isCollapsedStateActive ? 26 : 14
                    Layout.leftMargin: 24
                    Layout.rightMargin: 24
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter

                    spacing: 0

                    Connections {
                        objectName: "drawerConnections"

                        target: drawer
                        function onCursorEntered() {
                            if (drawer.isCollapsedStateActive) {
                                collapsedButtonChevron.backgroundColor = collapsedButtonChevron.hoveredColor
                                collapsedButtonHeader.opacity = 0.8
                            } else {
                                collapsedButtonHeader.opacity = 1
                            }
                        }

                        function onCursorExited() {
                            if (drawer.isCollapsedStateActive) {
                                collapsedButtonChevron.backgroundColor = collapsedButtonChevron.defaultColor
                                collapsedButtonHeader.opacity = 1
                            } else {
                                collapsedButtonHeader.opacity = 1
                            }
                        }

                        function onPressed(pressed, entered) {
                            if (drawer.isCollapsedStateActive) {
                                collapsedButtonChevron.backgroundColor = pressed ? collapsedButtonChevron.pressedColor : entered ? collapsedButtonChevron.hoveredColor : collapsedButtonChevron.defaultColor
                                collapsedButtonHeader.opacity = 0.7
                            } else {
                                collapsedButtonHeader.opacity = 1
                            }
                        }
                    }

                    Header1TextType {
                        id: collapsedButtonHeader
                        objectName: "collapsedButtonHeader"

                        Layout.maximumWidth: drawer.width - 48 - 18 - 12

                        maximumLineCount: 2
                        elide: Qt.ElideRight

                        text: ServersModel.defaultServerName
                        horizontalAlignment: Qt.AlignHCenter

                        Behavior on opacity {
                            PropertyAnimation { duration: 200 }
                        }
                    }

                    ImageButtonType {
                        id: collapsedButtonChevron
                        objectName: "collapsedButtonChevron"

                        Layout.leftMargin: 8

                        visible: drawer.isCollapsedStateActive()

                        hoverEnabled: false
                        image: "qrc:/images/controls/chevron-down.svg"
                        imageColor: AmneziaStyle.color.paleGray

                        icon.width: 18
                        icon.height: 18
                        backgroundRadius: 16
                        horizontalPadding: 4
                        topPadding: 4
                        bottomPadding: 3

                        Keys.onEnterPressed: this.clicked()
                        Keys.onReturnPressed: this.clicked()

                        onClicked: {
                            if (drawer.isCollapsedStateActive()) {
                                drawer.openTriggered()
                            }
                        }
                    }
                }

                RowLayout {
                    objectName: "rowLayoutLabel"
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                    Layout.topMargin: 8
                    Layout.bottomMargin: drawer.isCollapsedStateActive ? 44 : ServersModel.isDefaultServerFromApi ? 61 : 16
                    spacing: 0

                    BasicButtonType {
                        enabled: (ServersModel.defaultServerImagePathCollapsed !== "") && drawer.isCollapsedStateActive
                        hoverEnabled: enabled

                        implicitHeight: 36

                        leftPadding: 16
                        rightPadding: 16

                        defaultColor: AmneziaStyle.color.transparent
                        hoveredColor: AmneziaStyle.color.translucentWhite
                        pressedColor: AmneziaStyle.color.sheerWhite
                        disabledColor: AmneziaStyle.color.transparent
                        textColor: AmneziaStyle.color.mutedGray

                        buttonTextLabel.lineHeight: 16
                        buttonTextLabel.font.pixelSize: 13
                        buttonTextLabel.font.weight: 400

                        // on the collapsed home card keep the row (card geometry) but show no text/icons
                        text: drawer.isCollapsedStateActive ? "" : ServersModel.defaultServerDescriptionExpanded
                        leftImageSource: drawer.isCollapsedStateActive ? "" : ServersModel.defaultServerImagePathCollapsed
                        leftImageColor: ""
                        changeLeftImageSize: false

                        rightImageSource: drawer.isCollapsedStateActive ? "" : (hoverEnabled ? "qrc:/images/controls/chevron-down.svg" : "")

                        Keys.onEnterPressed: this.clicked()
                        Keys.onReturnPressed: this.clicked()

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
            }

            ColumnLayout {
                id: serversMenuHeader
                objectName: "serversMenuHeader"

                anchors.top: collapsed.bottom
                anchors.right: parent.right
                anchors.left: parent.left

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                    spacing: 8

                    visible: !ServersModel.isDefaultServerFromApi

                    DropDownType {
                        id: containersDropDown
                        objectName: "containersDropDown"

                        rootButtonImageColor: AmneziaStyle.color.midnightBlack
                        rootButtonBackgroundColor: AmneziaStyle.color.paleGray
                        rootButtonBackgroundHoveredColor: AmneziaStyle.color.mistyGray
                        rootButtonBackgroundPressedColor: AmneziaStyle.color.cloudyGray
                        rootButtonHoveredBorderColor: AmneziaStyle.color.transparent
                        rootButtonDefaultBorderColor: AmneziaStyle.color.transparent
                        rootButtonTextTopMargin: 8
                        rootButtonTextBottomMargin: 8

                        enabled: drawer.isOpened

                        text: ServersModel.defaultServerDefaultContainerName
                        textColor: AmneziaStyle.color.midnightBlack
                        headerText: qsTr("VPN protocol")
                        headerBackButtonImage: "qrc:/images/controls/arrow-left.svg"

                        rootButtonClickedFunction: function() {
                            containersDropDown.openTriggered()
                        }

                        drawerParent: root

                        listView: HomeContainersListView {
                            id: containersListView
                            objectName: "containersListView"

                            rootWidth: root.width

                            Connections {
                                objectName: "rowLayoutConnections"

                                target: ServersModel

                                function onDefaultServerIndexChanged() {
                                    updateContainersModelFilters()
                                }
                            }

                            function updateContainersModelFilters() {
                                if (ServersModel.isDefaultServerHasWriteAccess()) {
                                    proxyDefaultServerContainersModel.filters = ContainersModelFilters.getWriteAccessProtocolsListFilters()
                                } else {
                                    proxyDefaultServerContainersModel.filters = ContainersModelFilters.getReadAccessProtocolsListFilters()
                                }
                            }

                            model: SortFilterProxyModel {
                                id: proxyDefaultServerContainersModel
                                sourceModel: DefaultServerContainersModel

                                sorters: [
                                    RoleSorter { roleName: "isInstalled"; sortOrder: Qt.DescendingOrder }
                                ]
                            }

                            Component.onCompleted: updateContainersModelFilters()
                        }
                    }
                }
            }

            ButtonGroup {
                id: serversRadioButtonGroup
                objectName: "serversRadioButtonGroup"
            }

            ServersListView {
                id: serversMenuContent
                objectName: "serversMenuContent"

                isFocusable: false

                Connections {
                    target: drawer

                    // this item shouldn't be focused when drawer is closed
                    function onIsOpenedChanged() {
                        serversMenuContent.isFocusable = drawer.isOpened
                    }
                }
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
            if (connected && !root.wasConnected && !ConnectionController.isConnectionInProgress) {
                root.startPterodactylFlight()
            } else if (!connected && root.wasConnected) {
                connectButton.birdPerched = false
                pterodactylFlight.stop()
                pterodactyl.opacity = 0
            } else if (connected && root.wasConnected) {
                // already connected (e.g. restored state) — make sure the bird stays perched
                connectButton.birdPerched = true
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
