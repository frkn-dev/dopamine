import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

DrawerType2 {
    id: root

    property bool isAppSplitTinnelingEnabled: Qt.platform.os === "windows" || Qt.platform.os === "android"

    anchors.fill: parent
    expandedHeight: parent.height * 0.9

    // toggle rows get a soft brand tint when enabled
    component ToggleCard: Rectangle {
        property alias checked: toggle.checked
        property alias text: toggle.text
        signal toggled(bool checked)

        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.rightMargin: 8

        radius: 8
        color: toggle.checked ? DopamineStyle.color.translucentRichBrown : DopamineStyle.color.transparent
        implicitHeight: toggle.implicitHeight + 8

        Behavior on color { ColorAnimation { duration: 150 } }

        SwitcherType {
            id: toggle

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 8
            anchors.rightMargin: 8

            onToggled: function() {
                parent.toggled(checked)
            }
        }
    }

    expandedStateContent: Flickable {
        id: contentFlickable

        width: root.width
        height: root.expandedHeight
        contentWidth: width
        contentHeight: content.implicitHeight
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ScrollBar.vertical: ScrollBarType { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: content

            width: contentFlickable.width
            spacing: 0

            Header2Type {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 16

                headerText: qsTr("Split tunneling")
                descriptionText:  qsTr("Allows you to connect to some sites or applications through a VPN connection and bypass others")
            }

            LabelWithButtonType {
                id: splitTunnelingSwitch
                Layout.fillWidth: true
                Layout.topMargin: 16

                visible: ServersModel.isDefaultServerDefaultContainerHasSplitTunneling

                text: qsTr("Split tunneling on the server")
                descriptionText: qsTr("Enabled \nCan't be disabled for current server")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsSplitTunneling)
                    root.closeTriggered()
                }
            }

            DividerType {
                visible: ServersModel.isDefaultServerDefaultContainerHasSplitTunneling
            }

            // --- sites & services: one switch, one direction (presets are
            // exceptions to the sites mode), separate lists ---

            ToggleCard {
                id: sitesToggle

                text: qsTr("Site and service split tunneling")
                checked: SitesModel.isTunnelingEnabled

                onToggled: function(checked) {
                    SitesModel.toggleSplitTunneling(checked)
                }
            }

            FilterDropDown {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: SitesModel.isTunnelingEnabled

                filterModel: sitesRouteModeModel
                currentValue: SitesModel.routeMode === 2 ? "bypass" : "via"

                onSelected: function(value) {
                    SitesModel.routeMode = value === "bypass" ? 2 : 1
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 4

                text: qsTr("Manage the site list")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsSplitTunneling)
                    root.closeTriggered()
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                // сервисные списки — экспериментальная фича: без неё строки нет
                visible: SettingsController.isExperimentalFeaturesEnabled

                text: qsTr("Manage the service list")
                descriptionText: SplitPresetsModel.enabledCount > 0 ? qsTr("Enabled") : qsTr("Disabled")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsSplitPresets)
                    root.closeTriggered()
                }
            }

            DividerType {
            }

            // --- apps: own switch + own direction ---

            ToggleCard {
                id: appsToggle

                visible: isAppSplitTinnelingEnabled

                text: qsTr("App-based split tunneling")
                checked: AppSplitTunnelingModel.isTunnelingEnabled

                onToggled: function(checked) {
                    AppSplitTunnelingModel.toggleSplitTunneling(checked)
                }
            }

            FilterDropDown {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                // Windows supports app exclusions only (WFP driver); both
                // directions work on Android
                visible: isAppSplitTinnelingEnabled && AppSplitTunnelingModel.isTunnelingEnabled
                         && Qt.platform.os === "android"

                filterModel: appsRouteModeModel
                currentValue: AppSplitTunnelingModel.routeMode === 2 ? "bypass" : "via"

                onSelected: function(value) {
                    AppSplitTunnelingModel.routeMode = value === "bypass" ? 2 : 1
                }
            }

            LabelWithButtonType {
                id: appSplitTunnelingSwitch
                visible: isAppSplitTinnelingEnabled

                Layout.fillWidth: true
                Layout.topMargin: 4

                text: qsTr("Manage the app list")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsAppSplitTunneling)
                    root.closeTriggered()
                }
            }

            DividerType {
                visible: isAppSplitTinnelingEnabled
            }
        }
    }

    ListModel {
        id: sitesRouteModeModel
    }

    ListModel {
        id: appsRouteModeModel
    }

    Component.onCompleted: {
        sitesRouteModeModel.append({ "name": qsTr("via VPN"), "value": "via" })
        sitesRouteModeModel.append({ "name": qsTr("bypass VPN"), "value": "bypass" })
        appsRouteModeModel.append({ "name": qsTr("via VPN"), "value": "via" })
        appsRouteModeModel.append({ "name": qsTr("bypass VPN"), "value": "bypass" })
    }
}
