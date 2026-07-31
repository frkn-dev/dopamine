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
            return "AmneziaWG"
        }
        if (proto === "wireguard") {
            return "WireGuard"
        }
        return proto
    }

    function rebuildProtocolsModel() {
        const protos = ServersModel.availableProtocols

        if (!root.protocolFilterTouched) {
            root.protocolFilter = protos.indexOf("awg") >= 0 ? "awg" : ""
        } else if (protos.length > 0 && root.protocolFilter !== "" && protos.indexOf(root.protocolFilter) < 0) {
            root.protocolFilter = ""
        }

        protocolsModel.clear()
        protocolsModel.append({ "name": protocolLabel(""), "value": "" })
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

    Component.onCompleted: {
        const saved = SettingsController.serversProtocolFilter
        if (saved !== "") {
            root.protocolFilterTouched = true
            root.protocolFilter = saved === "all" ? "" : saved
        }
        const savedEnv = SettingsController.serversEnvFilter
        if (savedEnv !== "") {
            root.envFilter = savedEnv === "all" ? "" : savedEnv
        }
        rebuildProtocolsModel()
        rebuildEnvsModel()
    }

    header: Item {
        visible: ServersModel.availableProtocols.length > 1 || ServersModel.availableEnvs.length > 1
        width: root.width
        height: visible ? filtersRow.implicitHeight + 8 : 0

        RowLayout {
            id: filtersRow

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 16
            anchors.rightMargin: 16

            spacing: 8

            FilterDropDown {
                id: protocolFilterDropDown

                Layout.fillWidth: true

                visible: ServersModel.availableProtocols.length > 1

                filterModel: protocolsModel
                currentValue: root.protocolFilter

                onSelected: function(value) {
                    root.protocolFilterTouched = true
                    root.protocolFilter = value
                    SettingsController.serversProtocolFilter = value === "" ? "all" : value
                }
            }

            FilterDropDown {
                id: envFilterDropDown

                Layout.fillWidth: true

                visible: ServersModel.availableEnvs.length > 1

                filterModel: envsModel
                currentValue: root.envFilter

                onSelected: function(value) {
                    root.envFilter = value
                    SettingsController.serversEnvFilter = value === "" ? "all" : value
                }
            }
        }
    }

    model: SortFilterProxyModel {
        id: proxyServersModel
        sourceModel: ServersModel

        filters: [
            ValueFilter {
                roleName: "serviceProtocol"
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
                    descriptionText: serverDescription

                    checked: index === proxyServersModel.mapFromSource(root.selectedIndex)
                    checkable: true

                    ButtonGroup.group: serversRadioButtonGroup

                    onClicked: {
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
