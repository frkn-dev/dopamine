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

ListViewType {
    id: root

    property int selectedIndex: ServersModel.defaultIndex
    property string protocolFilter: ""
    property bool protocolFilterTouched: false
    property string envFilter: ""

    anchors.top: serversMenuHeader.bottom
    anchors.right: parent.right
    anchors.left: parent.left
    anchors.bottom: parent.bottom
    anchors.topMargin: 16

    Component.onCompleted: {
        const saved = SettingsController.serversProtocolFilter
        if (saved !== "" && saved !== "all") {
            root.protocolFilterTouched = true
            root.protocolFilter = saved
        }
        const savedEnv = SettingsController.serversEnvFilter
        if (savedEnv !== "" && savedEnv !== "all") {
            root.envFilter = savedEnv
        }
        rebuildProtocolsModel()
        rebuildEnvsModel()

        // probing is meaningful only with VPN off (see healthcheck research)
        if (!ConnectionController.isConnected) {
            HealthCheckController.startProbe()
        }
    }

    function flagCountryCode(cc) {
        // backend sometimes sends non-ISO codes
        const aliases = { "SWE": "SE", "HEL": "FI", "UK": "GB" }
        return aliases[cc] !== undefined ? aliases[cc] : cc
    }

    function protocolLabel(proto) {
        if (proto === "") {
            return qsTr("All")
        }
        if (proto === "vless") {
            return "VLESS"
        }
        if (proto === "hysteria2") {
            return "Hysteria2"
        }
        if (proto === "awg") {
            return "AWG"
        }
        if (proto === "awg-mobile" || proto === "amneziawgmobile") {
            return "AWG Mobile"
        }
        if (proto === "wireguard") {
            return "WireGuard"
        }
        return proto
    }

    function rebuildProtocolsModel() {
        // only protocols the selected env actually has — no dead options
        const protos = ServersModel.availableProtocolsForEnv(root.envFilter)

        // same priority as auto-select: mobile AWG first, then plain AWG
        function pickDefault() {
            if (protos.indexOf("awg-mobile") >= 0) return "awg-mobile"
            if (protos.indexOf("awg") >= 0) return "awg"
            return protos.length > 0 ? protos[0] : ""
        }

        if (!root.protocolFilterTouched) {
            root.protocolFilter = pickDefault()
        } else if (protos.length > 0 && protos.indexOf(root.protocolFilter) < 0) {
            root.protocolFilter = pickDefault()
        }

        protocolsModel.clear()
        for (let i = 0; i < protos.length; i++) {
            protocolsModel.append({ "name": protocolLabel(protos[i]), "value": protos[i] })
        }
    }

    ListModel {
        id: protocolsModel
    }

    function envLabel(env) {
        if (env === "") {
            return qsTr("All")
        }
        if (env === "wl") {
            return qsTr("White Elephants")
        }
        if (env === "dev") {
            return qsTr("Regular")
        }
        if (env === "ru") {
            return qsTr("Reverse")
        }
        return env
    }

    function rebuildEnvsModel() {
        const envs = ServersModel.availableEnvs

        if (envs.length > 0 && root.envFilter !== "" && envs.indexOf(root.envFilter) < 0) {
            root.envFilter = ""
        }

        envsModel.clear()
        envsModel.append({ "name": envLabel(""), "value": "" })
        for (let i = 0; i < envs.length; i++) {
            envsModel.append({ "name": envLabel(envs[i]), "value": envs[i] })
        }
    }

    ListModel {
        id: envsModel
    }

    header: Item {
        visible: true
        width: root.width
        height: visible ? headerColumn.implicitHeight + 8 : 0

        ColumnLayout {
            id: headerColumn

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8

            RowLayout {
                id: filtersRow

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                spacing: 8

                FilterDropDown {
                    id: envFilterDropDown

                    Layout.fillWidth: true

                    visible: ServersModel.availableEnvs.length > 1

                    filterModel: envsModel
                    currentValue: root.envFilter

                    onSelected: function(value) {
                        root.envFilter = value
                        SettingsController.serversEnvFilter = value === "" ? "all" : value
                        // protocol options depend on the env
                        root.rebuildProtocolsModel()
                    }
                }

                FilterDropDown {
                    id: protocolFilterDropDown

                    Layout.fillWidth: true

                    visible: protocolsModel.count > 1

                    filterModel: protocolsModel
                    currentValue: root.protocolFilter

                    onSelected: function(value) {
                        root.protocolFilterTouched = true
                        root.protocolFilter = value
                        SettingsController.serversProtocolFilter = value === "" ? "all" : value
                    }
                }

                ImageButtonType {
                    id: refreshHealthButton

                    Layout.alignment: Qt.AlignVCenter

                    implicitWidth: 48
                    implicitHeight: 48

                    image: "qrc:/images/controls/gauge.svg"
                    imageColor: AmneziaStyle.color.paleGray

                    // probing with the VPN on measures the tunnel, not the servers —
                    // keep the button visible but disabled so it doesn't "disappear"
                    enabled: !ConnectionController.isConnected

                    onClicked: HealthCheckController.startProbe(true)
                }
            }

            VerticalRadioButton {
                id: autoSelectButton
                objectName: "autoSelectButton"

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Auto-select")
                descriptionText: qsTr("Fastest available server")

                checked: SettingsController.autoServerSelection
                checkable: true

                ButtonGroup.group: serversRadioButtonGroup

                onClicked: {
                    SettingsController.autoServerSelection = true

                    // connected already — reconnect to the auto-picked server
                    if (ConnectionController.isConnected) {
                        ConnectionController.openConnection()
                    }
                }

                Keys.onEnterPressed: autoSelectButton.clicked()
                Keys.onReturnPressed: autoSelectButton.clicked()
            }
        }
    }

    model: SortFilterProxyModel {
        id: proxyServersModel
        sourceModel: ServersModel

        filters: [
            ValueFilter {
                roleName: "serviceProtocolFilter"
                value: root.protocolFilter
                enabled: root.protocolFilter !== ""
            },
            ValueFilter {
                roleName: "connectionEnv"
                value: root.envFilter
                enabled: root.envFilter !== ""
            }
        ]
    }

    Connections {
        target: ServersModel
        function onDefaultServerIndexChanged(serverIndex) {
            root.selectedIndex = serverIndex
        }
        function onAvailableProtocolsChanged() {
            root.rebuildProtocolsModel()
        }
        function onAvailableEnvsChanged() {
            root.rebuildEnvsModel()
        }
    }

    delegate: Item {
        id: menuContentDelegate
        objectName: "menuContentDelegate"

        property variant delegateData: model
        property VerticalRadioButton serverRadioButtonProperty: serverRadioButton

        implicitWidth: root.width
        implicitHeight: serverRadioButtonContent.implicitHeight

        ColumnLayout {
            id: serverRadioButtonContent
            objectName: "serverRadioButtonContent"

            anchors.fill: parent
            anchors.rightMargin: 16
            anchors.leftMargin: 16

            spacing: 0

            RowLayout {
                objectName: "serverRadioButtonRowLayout"

                Layout.fillWidth: true

                Image {
                    id: serverCountryFlag

                    objectName: "serverCountryFlag"

                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24

                    sourceSize.width: 48
                    sourceSize.height: 48
                    fillMode: Image.PreserveAspectFit

                    visible: countryCode !== "" && status !== Image.Error
                    source: visible ? "qrc:/countriesFlags/images/flagKit/" + root.flagCountryCode(countryCode) + ".svg" : ""

                    layer.enabled: true
                    layer.effect: OpacityMask {
                        maskSource: Rectangle {
                            width: serverCountryFlag.width
                            height: serverCountryFlag.height
                            radius: 5
                        }
                    }
                }

                VerticalRadioButton {
                    id: serverRadioButton
                    objectName: "serverRadioButton"

                    Layout.fillWidth: true

                    text: name
                    // no address/protocol/country here — technical details live
                    // on the server details card; legacy servers keep services
                    descriptionText: isServerFromGatewayApi ? "" : serverDescription

                    checked: index === proxyServersModel.mapFromSource(root.selectedIndex) && !SettingsController.autoServerSelection
                    checkable: true

                    ButtonGroup.group: serversRadioButtonGroup

                    onClicked: {
                        SettingsController.autoServerSelection = false

                        root.selectedIndex = proxyServersModel.mapToSource(index)

                        ServersModel.defaultIndex = root.selectedIndex

                        // connected already — reconnect to the newly selected server
                        if (ConnectionController.isConnected) {
                            ConnectionController.openConnection()
                        }
                    }

                    Keys.onEnterPressed: serverRadioButton.clicked()
                    Keys.onReturnPressed: serverRadioButton.clicked()
                }

                RowLayout {
                    objectName: "serverHealthBadge"

                    Layout.alignment: Qt.AlignVCenter
                    Layout.rightMargin: 8
                    spacing: 6

                    visible: healthLatency !== -2

                    Rectangle {
                        Layout.alignment: Qt.AlignVCenter
                        width: 10
                        height: 10
                        radius: 5

                        visible: healthLatency >= 0

                        color: healthLatency < 120 ? "#34C759" : (healthLatency < 300 ? "#FF9F0A" : "#FF453A")
                    }

                    Image {
                        Layout.alignment: Qt.AlignVCenter
                        sourceSize.width: 12
                        sourceSize.height: 12

                        visible: healthLatency === -1

                        source: "qrc:/images/controls/close.svg"

                        layer.enabled: true
                        layer.effect: ColorOverlay {
                            color: "#FF453A"
                        }
                    }

                    CaptionTextType {
                        Layout.alignment: Qt.AlignVCenter

                        visible: SettingsController.isServerPingTextVisible

                        text: healthLatency >= 0 ? healthLatency + " ms" : qsTr("offline")
                        color: healthLatency >= 0
                               ? (healthLatency < 120 ? "#34C759" : (healthLatency < 300 ? "#FF9F0A" : "#FF453A"))
                               : AmneziaStyle.color.vibrantRed
                    }
                }

                ImageButtonType {
                    id: serverInfoButton
                    objectName: "serverInfoButton"

                    image: "qrc:/images/controls/settings.svg"
                    imageColor: AmneziaStyle.color.paleGray

                    implicitWidth: 56
                    implicitHeight: 56

                    z: 1

                    onClicked: function() {
                        ServersModel.processedIndex = proxyServersModel.mapToSource(index)

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

                        drawer.closeTriggered()
                    }
                }
            }

            DividerType {
                Layout.fillWidth: true
                Layout.leftMargin: 0
                Layout.rightMargin: 0
            }
        }
    }
}
