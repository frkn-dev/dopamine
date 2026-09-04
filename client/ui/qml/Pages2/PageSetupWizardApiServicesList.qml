import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import SortFilterProxyModel 0.2

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

PageType {
    id: root

    BackButtonType {
        id: backButton

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin

        onActiveFocusChanged: {
            if(backButton.enabled && backButton.activeFocus) {
                listView.positionViewAtBeginning()
            }
        }
    }

    ListViewType {
        id: listView

        anchors.top: backButton.bottom
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.topMargin: 16

        header: ColumnLayout {
            width: listView.width
            spacing: 16

            BaseHeaderType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 8

                headerText: qsTr("VPN by FRKN")
                descriptionText: qsTr("Choose a VPN service that suits your needs.")
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                text: qsTr("I have a subscription ID")
            }

            TextFieldWithHeaderType {
                id: subscriptionIdField
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                headerText: qsTr("Subscription ID")
                textField.placeholderText: qsTr("Enter UUID")

                textField.onTextChanged: {
                    ApiConfigsController.setSubscriptionId(textField.text)
                }
            }

            DividerType {
                Layout.topMargin: 8
                Layout.bottomMargin: 8
            }

            ParagraphTextType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                text: qsTr("No subscription?")
            }

            TextFieldWithHeaderType {
                id: trialEmailField
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                headerText: qsTr("Email (optional)")
                textField.placeholderText: qsTr("For trial activation letter")
            }

            BasicButtonType {
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 8
                text: qsTr("Create trial account")
                clickedFunc: function() {
                    ApiConfigsController.createTrial(trialEmailField.textField.text)
                }
            }

            ParagraphTextType {
                id: subscriptionStatusLabel
                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 8
                text: ApiConfigsController.subscriptionId === "" ? qsTr("No subscription selected") : qsTr("Subscription: %1").arg(ApiConfigsController.subscriptionId)
                color: DopamineStyle.color.mutedGray
            }
        }

        spacing: 0

        model: SortFilterProxyModel {
            id: proxyApiServicesModel

            sourceModel: ApiServicesModel
            sorters: RoleSorter {
                roleName: "order"
                sortOrder: Qt.AscendingOrder
            }
        }

        delegate: ColumnLayout {

            width: listView.width

            enabled: isServiceAvailable

            CardWithIconsType {
                id: card

                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16
                Layout.bottomMargin: 16

                headerText: name
                bodyText: cardDescription
                footerText: price === qsTr("Free") ? "" : price

                rightImageSource: "qrc:/images/controls/chevron-right.svg"

                onClicked: {
                    if (isServiceAvailable) {
                        ApiServicesModel.setServiceIndex(proxyApiServicesModel.mapToSource(index))
                        ApiConfigsController.setSelectedServerCountryCode("")
                        ApiConfigsController.setImportAllCountries(false)
                        PageController.goToPage(PageEnum.PageSetupWizardApiServiceInfo)
                    }
                }
                
                Keys.onEnterPressed: clicked()
                Keys.onReturnPressed: clicked()
            }
        }
    }
}
