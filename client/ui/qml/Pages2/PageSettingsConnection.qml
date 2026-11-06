import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Config"

PageType {
    id: root

    // Скролл сбрасываем при открытии страницы, а не по фокусу кнопки «назад»:
    // фокус переезжает во время обычных свайпов, и список возвращало наверх.
    onVisibleChanged: {
        if (visible) {
            listView.positionViewAtBeginning()
        }
    }

    // Клиентский MTU: пусто/0 = брать из конфига сервера. Значение больше пути
    // даёт не ошибку подключения, а «половину неработающих сайтов», поэтому
    // выбор пресетами, а не свободным вводом.
    QtObject {
        id: mtuAuto
        readonly property string name: qsTr("Auto (from server)")
        readonly property int value: 0
    }
    QtObject {
        id: mtu1280
        readonly property string name: "1280"
        readonly property int value: 1280
    }
    QtObject {
        id: mtu1300
        readonly property string name: "1300"
        readonly property int value: 1300
    }
    QtObject {
        id: mtu1360
        readonly property string name: "1360"
        readonly property int value: 1360
    }
    QtObject {
        id: mtu1380
        readonly property string name: "1380"
        readonly property int value: 1380
    }
    QtObject {
        id: mtu1420
        readonly property string name: "1420"
        readonly property int value: 1420
    }
    QtObject {
        id: mtu1450
        readonly property string name: "1450"
        readonly property int value: 1450
    }
    QtObject {
        id: mtu1500
        readonly property string name: "1500"
        readonly property int value: 1500
    }

    property list<QtObject> mtuOptions: [mtuAuto, mtu1280, mtu1300, mtu1360, mtu1380, mtu1420, mtu1450, mtu1500]

    property int selectedMtu: SettingsController.mtuOverride

    function mtuOptionIndex(value) {
        for (var i = 0; i < mtuOptions.length; i++) {
            if (mtuOptions[i].value === value) {
                return i
            }
        }
        return 0
    }

    function mtuOptionText() {
        var index = mtuOptionIndex(selectedMtu)
        if (mtuOptions[index].value > 0) {
            return mtuOptions[index].name
        }
        var serverMtu = ApiConfigsController.getCurrentServerMtu()
        return mtuOptions[0].name + (serverMtu !== "" ? " · " + serverMtu : "")
    }

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

        header: ColumnLayout {

            width: listView.width

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                headerText: qsTr("Connection")
            }
        }

        model: 1 // fake model to force the ListView to be created without a model

        delegate: ColumnLayout { // TODO(CyAn84): add DelegateChooser when have migrated to 6.9

            width: listView.width

            SwitcherType {
                id: dnsOverrideSwitch

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Change DNS")
                descriptionText: qsTr("Overrides server DNS for every connection. Applies after reconnect.")
                checked: SettingsController.dnsOverrideEnabled

                onToggled: function() {
                    SettingsController.dnsOverrideEnabled = checked
                    checked = Qt.binding(function() { return SettingsController.dnsOverrideEnabled })
                    PageController.showNotificationMessage(qsTr("Saved — applies after you reconnect the VPN"))
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8
                spacing: 12

                visible: SettingsController.dnsOverrideEnabled

                TextFieldWithHeaderType {
                    id: customPrimaryDns

                    Layout.fillWidth: true
                    headerText: qsTr("Primary DNS")
                    textField.text: SettingsController.primaryDns
                    textField.validator: RegularExpressionValidator {
                        regularExpression: InstallController.ipAddressRegExp()
                    }
                }

                TextFieldWithHeaderType {
                    id: customSecondaryDns

                    Layout.fillWidth: true
                    headerText: qsTr("Secondary DNS")
                    textField.text: SettingsController.secondaryDns
                    textField.validator: RegularExpressionValidator {
                        regularExpression: InstallController.ipAddressRegExp()
                    }
                }

                BasicButtonType {
                    Layout.fillWidth: true
                    implicitHeight: 40

                    text: qsTr("Save DNS")
                    enabled: customPrimaryDns.textField.text !== ""
                             && customSecondaryDns.textField.text !== ""
                             && customPrimaryDns.textField.acceptableInput
                             && customSecondaryDns.textField.acceptableInput

                    clickedFunc: function() {
                        SettingsController.primaryDns = customPrimaryDns.textField.text
                        SettingsController.secondaryDns = customSecondaryDns.textField.text
                        PageController.showNotificationMessage(qsTr("Saved — applies after you reconnect the VPN"))
                    }
                }
            }

            DividerType {}

            SwitcherType {
                id: mtuOverrideSwitch

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Change MTU")
                descriptionText: qsTr("Overrides server MTU for WireGuard/AmneziaWG. Applies after reconnect.")
                checked: SettingsController.mtuOverride > 0

                onToggled: function() {
                    if (checked === (SettingsController.mtuOverride > 0)) {
                        return
                    }
                    if (checked) {
                        var value = root.selectedMtu > 0 ? root.selectedMtu : 1300
                        SettingsController.mtuOverride = value
                        root.selectedMtu = value
                    } else {
                        SettingsController.mtuOverride = 0
                        root.selectedMtu = 0
                    }
                    checked = Qt.binding(function() { return SettingsController.mtuOverride > 0 })
                    PageController.showNotificationMessage(qsTr("Saved — applies after you reconnect the VPN"))
                }
            }

            DropDownType {
                id: mtuSelector

                Layout.fillWidth: true
                Layout.topMargin: 0
                Layout.bottomMargin: 8
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: SettingsController.mtuOverride > 0

                drawerParent: root

                headerText: qsTr("MTU value")
                text: root.mtuOptionText()

                listView: ListViewWithRadioButtonType {
                    rootWidth: root.width

                    model: root.mtuOptions
                    selectedIndex: root.mtuOptionIndex(root.selectedMtu)

                    clickedFunction: function() {
                        root.selectedMtu = root.mtuOptions[selectedIndex].value
                        mtuSelector.closeTriggered()
                    }
                }
            }

            BasicButtonType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8
                implicitHeight: 40

                visible: SettingsController.mtuOverride > 0

                text: qsTr("Save MTU")
                enabled: root.selectedMtu !== SettingsController.mtuOverride

                clickedFunc: function() {
                    SettingsController.mtuOverride = root.selectedMtu
                    PageController.showNotificationMessage(qsTr("Saved — applies after you reconnect the VPN"))
                }
            }

            DividerType {}

            SwitcherType {
                id: amneziaDnsSwitch

                visible: false

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Use DopamineDNS")
                descriptionText: qsTr("If DopamineDNS is installed on the server")

                checked: SettingsController.isAmneziaDnsEnabled()
                onToggled: function() {
                    if (checked !== SettingsController.isAmneziaDnsEnabled()) {
                        SettingsController.toggleAmneziaDns(checked)
                    }
                }
            }

            DividerType {
                visible: false
            }

            SwitcherType {
                id: routeLanSwitch

                visible: !GC.isMobile()

                Layout.fillWidth: true
                Layout.margins: 16

                text: qsTr("Route local network through VPN")
                descriptionText: qsTr("When off, devices in your local network (SSH, printers, shared folders) stay reachable while the VPN is on. Applies on the next connection.")

                checked: SettingsController.isRouteLanThroughVpn
                onToggled: function() {
                    if (checked !== SettingsController.isRouteLanThroughVpn) {
                        SettingsController.toggleRouteLanThroughVpn(checked)
                    }
                }
            }


            DividerType {}

        }

        footer: ColumnLayout { // TODO(CyAn84): move to delegate,add DelegateChooser when have migrated to 6.9

            width: listView.width

            LabelWithButtonType {
                id: killSwitchButton
                visible: !GC.isMobile()

                Layout.fillWidth: true

                text: qsTr("KillSwitch")
                descriptionText: qsTr("Blocks network connections without VPN")
                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                clickedFunction: function() {
                    PageController.goToPage(PageEnum.PageSettingsKillSwitch)
                }
            }

            DividerType {
                visible: GC.isDesktop()
            }
        }
    }
}
