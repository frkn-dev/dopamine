import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

PageType {
    id: root

    ListModel {
        id: protocolsModel
    }

    Component.onCompleted: {
        protocolsModel.clear()
        var configs = ApiConfigsController.subscriptionConfigs
        console.log("[SUB PROTOCOLS] filling model, configs count:", configs.length)
        for (var i = 0; i < configs.length; i++) {
            var info = configs[i].displayInfo || {}
            protocolsModel.append({
                countryCode: info.countryCode || "",
                countryName: info.countryName || "",
                protocol: info.protocol || "",
                hostName: info.hostName || "",
                serviceName: info.serviceName || "",
                connectionUuid: info.connectionUuid || "",
                connectionLabel: info.connectionLabel || "",
                displayIndex: info.displayIndex || 0,
                checked: true,
                originalIndex: i
            })
        }
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
        anchors.right: parent.right
        anchors.left: parent.left

        header: ColumnLayout {
            width: listView.width

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 24

                headerText: qsTr("Select protocols")
                descriptionText: qsTr("Choose which configurations to import")
            }
        }

        model: protocolsModel

        delegate: ColumnLayout {
            width: listView.width
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 12
                Layout.bottomMargin: 12
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 12

                Item {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32

                    CheckBoxType {
                        id: protocolCheckBox
                        anchors.centerIn: parent
                        width: 32
                        height: 32
                        checked: model.checked
                        onClicked: {
                            model.checked = !model.checked
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: {
                            var label = model.connectionLabel !== "" ? model.connectionLabel
                                                                       : (model.serviceName !== "" ? model.serviceName : model.countryCode + " · " + model.protocol)
                            if (model.displayIndex > 0) {
                                label += " #" + model.displayIndex
                            }
                            return label
                        }
                        color: AmneziaStyle.color.paleGray
                        font.pixelSize: 16
                        font.weight: 400
                        font.family: "PT Root UI VF"
                        lineHeight: 20 + LanguageModel.getLineHeightAppend()
                        lineHeightMode: Text.FixedHeight
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: model.countryCode + " · " + model.hostName
                        color: AmneziaStyle.color.mutedGray
                        font.pixelSize: 13
                        font.weight: 400
                        font.family: "PT Root UI VF"
                        lineHeight: 16 + LanguageModel.getLineHeightAppend()
                        lineHeightMode: Text.FixedHeight
                        wrapMode: Text.Wrap
                    }
                }
            }

            DividerType {
                Layout.fillWidth: true
            }
        }

        footer: ColumnLayout {
            width: listView.width

            BasicButtonType {
                Layout.fillWidth: true
                Layout.topMargin: 32
                Layout.bottomMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Import selected")

                clickedFunc: function() {
                    console.log("[SUB PROTOCOLS] import clicked, count:", protocolsModel.count)

                    var importedCount = 0
                    for (var i = 0; i < protocolsModel.count; i++) {
                        var item = protocolsModel.get(i)
                        console.log("[SUB PROTOCOLS] item", i, "checked:", item.checked)
                        if (item.checked) {
                            if (ApiConfigsController.installSubscriptionConfig(item.originalIndex)) {
                                importedCount++
                            }
                        }
                    }

                    console.log("[SUB PROTOCOLS] imported:", importedCount)
                    PageController.showNotificationMessage(qsTr("Imported %1 configurations").arg(importedCount))
                    PageController.goToPageHome()
                }
            }
        }
    }
}
