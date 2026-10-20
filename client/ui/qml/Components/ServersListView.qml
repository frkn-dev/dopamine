import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

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
    property string subscriptionFilter: ""
    // sentinel for servers that do not belong to any FRKN subscription
    readonly property string otherSubscriptionValue: "__other__"
    property var expandedGroups: ({})

    ButtonGroup {
        id: serversRadioButtonGroup
    }

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
        const savedSub = SettingsController.serversSubscriptionFilter
        if (savedSub !== "" && savedSub !== "all") {
            root.subscriptionFilter = savedSub
        }
        rebuildProtocolsModel()
        rebuildEnvsModel()
        rebuildSubscriptionsModel()
        rebuildGroups()

        if (!ConnectionController.isConnected) {
            HealthCheckController.startProbe()
        }
    }

    function flagCountryCode(cc) {
        const aliases = {
            "SWE": "SE", "HEL": "FI", "UK": "GB",
            "EST": "EE", "TLL": "EE", "EESTI": "EE", "ESTONIA": "EE", "TALLINN": "EE"
        }
        return aliases[cc] !== undefined ? aliases[cc] : cc
    }

    function protocolLabel(proto) {
        if (proto === "") return qsTr("All")
        if (proto === "vless") return "VLESS"
        if (proto === "hysteria2") return "Hysteria2"
        if (proto === "awg") return "AmneziaWG"
        if (proto === "awg-mobile" || proto === "amneziawgmobile") return "AmneziaWG Mobile"
        if (proto === "wireguard") return "WireGuard"
        return proto
    }

    function protocolRank(proto) {
        const p = (proto || "").toLowerCase()
        if (p === "awg-mobile" || p === "amneziawgmobile") return 0
        if (p === "awg") return 1
        if (p === "hysteria2") return 2
        if (p === "vless") return 3
        if (p === "wireguard") return 4
        return 5
    }

    function rebuildProtocolsModel() {
        const protos = ServersModel.availableProtocolsForEnv(root.envFilter)

        if (!root.protocolFilterTouched) {
            root.protocolFilter = ""
        } else if (root.protocolFilter !== "" && root.protocolFilter !== "all" && protos.indexOf(root.protocolFilter) < 0) {
            root.protocolFilter = ""
        }

        protocolsModel.clear()
        protocolsModel.append({ "name": qsTr("All"), "value": "" })
        for (let i = 0; i < protos.length; i++) {
            protocolsModel.append({ "name": protocolLabel(protos[i]), "value": protos[i] })
        }
    }

    ListModel {
        id: protocolsModel
    }

    function envLabel(env) {
        if (env === "") return qsTr("All")
        if (env === "wl") return qsTr("White Elephants")
        if (env === "dev") return qsTr("Regular")
        if (env === "ru") return qsTr("Reverse")
        if (env === "premium") return qsTr("Premium")
        if (env === "private") return qsTr("Private")
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

    // servers that are not part of any FRKN subscription (share links, imported
    // configs) — shown as a separate "Other" bucket so they don't vanish when a
    // concrete subscription is selected
    function hasNonSubscriptionServers() {
        const subs = ServersModel.availableSubscriptions
        for (let i = 0; i < ServersModel.getServersCount(); ++i) {
            const variant = ServersModel.getServerDisplayData(i)
            const id = variant.subscriptionId || ""
            if (id === "" || subs.indexOf(id) < 0) {
                return true
            }
        }
        return false
    }

    function rebuildSubscriptionsModel() {
        const subs = ServersModel.availableSubscriptions
        if (root.subscriptionFilter !== "" && root.subscriptionFilter !== "all"
                && root.subscriptionFilter !== root.otherSubscriptionValue
                && subs.indexOf(root.subscriptionFilter) < 0) {
            root.subscriptionFilter = ""
        }

        subscriptionsModel.clear()
        subscriptionsModel.append({ "name": qsTr("All"), "value": "" })
        for (let i = 0; i < subs.length; i++) {
            subscriptionsModel.append({ "name": qsTr("Subscription %1").arg(i + 1), "value": subs[i] })
        }
        if (hasNonSubscriptionServers()) {
            subscriptionsModel.append({ "name": qsTr("Other"), "value": root.otherSubscriptionValue })
        }
    }

    ListModel {
        id: subscriptionsModel
    }

    function variantMatches(variant) {
        if (root.protocolFilter !== "" && root.protocolFilter !== "all" && variant.protocol !== root.protocolFilter) {
            return false
        }
        if (root.envFilter !== "" && root.envFilter !== "all" && variant.env !== root.envFilter) {
            return false
        }
        const sub = root.subscriptionFilter
        if (sub !== "" && sub !== "all") {
            const id = variant.subscriptionId || ""
            if (sub === root.otherSubscriptionValue) {
                if (ServersModel.availableSubscriptions.indexOf(id) >= 0) {
                    return false
                }
            } else if (id !== sub) {
                return false
            }
        }
        return true
    }

    function rebuildGroups() {
        const previousExpanded = root.expandedGroups
        const expanded = Object.assign({}, previousExpanded)
        const byKey = Object.create(null)

        for (let i = 0; i < ServersModel.getServersCount(); ++i) {
            const variant = ServersModel.getServerDisplayData(i)
            if (!variantMatches(variant)) continue

            const pool = (variant.pool || "").trim()
            const title = variant.groupName || variant.name || variant.countryName || "FRKN"
            const groupIdentity = pool || title
            const key = (variant.subscriptionId || "") + "|" + groupIdentity + (variant.subscriptionId ? "" : "|" + (variant.connectionUuid || i))

            if (!byKey[key]) {
                byKey[key] = {
                    key: key,
                    title: title,
                    variants: [],
                    expanded: expanded[key] === true
                }
            }
            byKey[key].variants.push(variant)
        }

        const groups = []
        for (const key of Object.keys(byKey)) {
            const group = byKey[key]
            group.variants.sort(function(a, b) {
                return protocolRank(a.protocol) - protocolRank(b.protocol) || a.sourceIndex - b.sourceIndex
            })
            group.preferred = group.variants[0]
            for (const variant of group.variants) {
                if (variant.sourceIndex === root.selectedIndex) {
                    group.preferred = variant
                    break
                }
            }
            groups.push(group)
        }

        groups.sort(function(a, b) {
            return a.title.localeCompare(b.title) || a.key.localeCompare(b.key)
        })

        groupsModel.clear()
        for (const group of groups) {
            groupsModel.append(group)
        }
        root.expandedGroups = expanded
    }

    function toggleGroup(key) {
        const expanded = Object.assign({}, root.expandedGroups)
        expanded[key] = expanded[key] !== true
        root.expandedGroups = expanded
        rebuildGroups()
    }

    ListModel {
        id: groupsModel
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
                id: subscriptionFilterRow

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: ServersModel.availableSubscriptions.length > 1

                spacing: 8

                FilterDropDown {
                    id: subscriptionFilterDropDown

                    Layout.fillWidth: true

                    filterModel: subscriptionsModel
                    currentValue: root.subscriptionFilter

                    onSelected: function(value) {
                        root.subscriptionFilter = value
                        SettingsController.serversSubscriptionFilter = value === "" ? "all" : value
                        root.rebuildGroups()
                    }
                }
            }

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
                        root.rebuildProtocolsModel()
                        root.rebuildGroups()
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
                        root.rebuildGroups()
                    }
                }

                ImageButtonType {
                    id: refreshHealthButton

                    Layout.alignment: Qt.AlignVCenter

                    implicitWidth: 48
                    implicitHeight: 48

                    image: "qrc:/images/controls/gauge.svg"
                    imageColor: DopamineStyle.color.paleGray

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
                    if (ConnectionController.isConnected) {
                        ConnectionController.openConnection()
                    }
                }

                Keys.onEnterPressed: autoSelectButton.clicked()
                Keys.onReturnPressed: autoSelectButton.clicked()
            }
        }
    }

    model: groupsModel

    Connections {
        target: ApiConfigsController
        function onReloadSubscriptionConfigsFinished(success) {
            if (!success) return
            const savedEnv = SettingsController.serversEnvFilter
            root.envFilter = savedEnv === "" || savedEnv === "all" ? "" : savedEnv
            root.rebuildEnvsModel()
            const savedProto = SettingsController.serversProtocolFilter
            root.protocolFilterTouched = savedProto !== "" && savedProto !== "all"
            root.protocolFilter = root.protocolFilterTouched ? savedProto : ""
            root.rebuildProtocolsModel()
            const savedSub = SettingsController.serversSubscriptionFilter
            root.subscriptionFilter = savedSub === "" || savedSub === "all" ? "" : savedSub
            root.rebuildSubscriptionsModel()
            root.rebuildGroups()
        }
    }

    Connections {
        target: ServersModel
        function onDefaultServerIndexChanged(serverIndex) {
            root.selectedIndex = serverIndex
            root.rebuildGroups()
        }
        function onAvailableProtocolsChanged() {
            root.rebuildProtocolsModel()
            root.rebuildGroups()
        }
        function onAvailableEnvsChanged() {
            root.rebuildEnvsModel()
            root.rebuildGroups()
        }
        function onAvailableSubscriptionsChanged() {
            root.rebuildSubscriptionsModel()
            root.rebuildGroups()
        }
        function onDataChanged() {
            root.rebuildGroups()
        }
    }

    delegate: Item {
        id: groupDelegate

        property var groupData: model

        implicitWidth: root.width
        implicitHeight: groupContent.implicitHeight

        ColumnLayout {
            id: groupContent

            anchors.fill: parent
            anchors.rightMargin: 16
            anchors.leftMargin: 16
            spacing: 0

            RowLayout {
                visible: groupData.variants.length > 1
                implicitHeight: 56
                Layout.fillWidth: true

                VerticalRadioButton {
                    Layout.fillWidth: true
                    text: groupData.title
                    descriptionText: groupData.variants.length + "×"
                    checkable: false
                    onClicked: root.toggleGroup(groupData.key)
                }

                CaptionTextType {
                    Layout.alignment: Qt.AlignVCenter
                    text: groupData.expanded ? "−" : "+"
                    color: DopamineStyle.color.paleGray
                }
            }

            Repeater {
                model: groupData.expanded ? groupData.variants : [groupData.preferred]

                delegate: Item {
                    id: variantDelegate

                    property var variant: modelData

                    implicitWidth: groupContent.width
                    implicitHeight: variantContent.implicitHeight

                    ColumnLayout {
                        id: variantContent

                        anchors.fill: parent
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true

                            Image {
                                id: serverCountryFlag

                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24

                                sourceSize.width: 48
                                sourceSize.height: 48
                                fillMode: Image.PreserveAspectFit

                                source: variant.countryCode !== ""
                                        ? "qrc:/countriesFlags/images/flagKit/" + root.flagCountryCode(variant.countryCode) + ".svg"
                                        : ""
                                visible: variant.countryCode !== "" && status !== Image.Error

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

                                text: variant.name
                                descriptionText: groupData.variants.length > 1
                                                 ? root.protocolLabel(variant.protocol)
                                                 : (variant.isServerFromGatewayApi ? "" : variant.serverDescription)

                                checked: variant.sourceIndex === root.selectedIndex && !SettingsController.autoServerSelection
                                checkable: true

                                ButtonGroup.group: serversRadioButtonGroup

                                onClicked: {
                                    SettingsController.autoServerSelection = false
                                    root.selectedIndex = variant.sourceIndex
                                    ServersModel.defaultIndex = variant.sourceIndex

                                    if (ConnectionController.isConnected) {
                                        ConnectionController.openConnection()
                                    }
                                }

                                Keys.onEnterPressed: serverRadioButton.clicked()
                                Keys.onReturnPressed: serverRadioButton.clicked()
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.rightMargin: 8
                                spacing: 6

                                visible: variant.healthLatency !== -2

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: 10
                                    height: 10
                                    radius: 5

                                    visible: variant.healthLatency >= 0
                                    color: variant.healthLatency < 120 ? "#34C759" : (variant.healthLatency < 300 ? "#FF9F0A" : "#FF453A")
                                }

                                Image {
                                    Layout.alignment: Qt.AlignVCenter
                                    sourceSize.width: 12
                                    sourceSize.height: 12

                                    visible: variant.healthLatency === -1
                                    source: "qrc:/images/controls/close.svg"

                                    layer.enabled: true
                                    layer.effect: ColorOverlay {
                                        color: "#FF453A"
                                    }
                                }

                                CaptionTextType {
                                    Layout.alignment: Qt.AlignVCenter

                                    visible: SettingsController.isServerPingTextVisible
                                    text: variant.healthLatency >= 0 ? variant.healthLatency + " ms" : qsTr("offline")
                                    color: variant.healthLatency >= 0
                                           ? (variant.healthLatency < 120 ? "#34C759" : (variant.healthLatency < 300 ? "#FF9F0A" : "#FF453A"))
                                           : DopamineStyle.color.vibrantRed
                                }
                            }

                            ImageButtonType {
                                id: serverInfoButton
                                objectName: "serverInfoButton"

                                image: "qrc:/images/controls/settings.svg"
                                imageColor: DopamineStyle.color.paleGray

                                implicitWidth: 56
                                implicitHeight: 56

                                z: 1

                                onClicked: function() {
                                    ServersModel.processedIndex = variant.sourceIndex

                                    if (ServersModel.getProcessedServerData("isServerFromGatewayApi")) {
                                        PageController.showBusyIndicator(true)
                                        let result = ApiSettingsController.getAccountInfo(false)
                                        PageController.showBusyIndicator(false)
                                        if (!result) return
                                        PageController.goToPage(PageEnum.PageSettingsApiServerInfo)
                                    } else {
                                        PageController.goToPage(PageEnum.PageSettingsServerInfo)
                                    }
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
        }
    }
}
