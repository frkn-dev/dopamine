import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    property bool isAppSplitTunnelingEnabled: Qt.platform.os === "windows" || Qt.platform.os === "android"

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin
    }

    ListViewType {
        id: listView

        anchors.top: backButton.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        ScrollBar.vertical: ScrollBarType { policy: ScrollBar.AlwaysOn }

        header: ColumnLayout {
            width: listView.width

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16

                headerText: qsTr("Split tunneling")
                descriptionText: qsTr("Allows you to connect to some sites or applications through a VPN connection and bypass others")
            }
        }

        model: 1

        delegate: ColumnLayout {
            width: listView.width
            spacing: 0

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: ServersModel.isDefaultServerDefaultContainerHasSplitTunneling

                text: qsTr("Split tunneling on the server")
                descriptionText: qsTr("Enabled \nCan't be disabled for current server")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsSplitTunneling)
                }
            }

            DividerType {
                visible: ServersModel.isDefaultServerDefaultContainerHasSplitTunneling
            }

            SwitcherType {
                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Site and service split tunneling")
                checked: SitesModel.isTunnelingEnabled

                onToggled: function() {
                    SitesModel.toggleSplitTunneling(checked)
                }
            }

            FilterDropDown {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8

                visible: SitesModel.isTunnelingEnabled

                filterModel: sitesRouteModeModel
                currentValue: SitesModel.routeMode === 2 ? "bypass" : "via"

                onSelected: function(value) {
                    SitesModel.routeMode = value === "bypass" ? 2 : 1
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                text: qsTr("Manage the site list")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsSplitTunneling)
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: SettingsController.isExperimentalFeaturesEnabled

                text: qsTr("Manage the service list")
                descriptionText: SplitPresetsModel.enabledCount > 0 ? qsTr("Enabled") : qsTr("Disabled")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsSplitPresets)
                }
            }

            DividerType {}

            SwitcherType {
                Layout.fillWidth: true
                Layout.margins: 16

                visible: root.isAppSplitTunnelingEnabled

                text: qsTr("App-based split tunneling")
                checked: AppSplitTunnelingModel.isTunnelingEnabled

                onToggled: function() {
                    AppSplitTunnelingModel.toggleSplitTunneling(checked)
                }
            }

            FilterDropDown {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8

                visible: root.isAppSplitTunnelingEnabled && AppSplitTunnelingModel.isTunnelingEnabled
                         && Qt.platform.os === "android"

                filterModel: appsRouteModeModel
                currentValue: AppSplitTunnelingModel.routeMode === 2 ? "bypass" : "via"

                onSelected: function(value) {
                    AppSplitTunnelingModel.routeMode = value === "bypass" ? 2 : 1
                }
            }

            LabelWithButtonType {
                Layout.fillWidth: true

                visible: root.isAppSplitTunnelingEnabled

                text: qsTr("Manage the app list")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsAppSplitTunneling)
                }
            }

            DividerType {
                visible: root.isAppSplitTunnelingEnabled
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
