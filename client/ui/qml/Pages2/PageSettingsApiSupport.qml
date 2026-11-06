import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import SortFilterProxyModel 0.2

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    QtObject {
        id: telegram

        // Бэкенд может отдать "@name", "name", "t.me/name" или полный URL —
        // нормализуем, иначе в ссылке получалось t.me дважды (t.me/t.me/name).
        readonly property string handle: {
            var raw = ("" + ApiAccountInfoModel.getTelegramBotLink()).trim()
            raw = raw.replace(/^https?:\/\//i, "")
            raw = raw.replace(/^t\.me\//i, "")
            raw = raw.replace(/^@/, "")
            raw = raw.replace(/\/+$/, "")
            if (raw.toLowerCase() === "frkn_org") {
                return "frkn_support"
            }
            return raw !== "" ? raw : "frkn_support"
        }
        readonly property string title: qsTr("Telegram")
        readonly property string description: "@" + handle
        readonly property string link: "https://t.me/" + handle
    }

    QtObject {
        id: techSupport

        readonly property string title: qsTr("Email")
        readonly property string description: ApiAccountInfoModel.getEmailLink()
        readonly property string link: "mailto:" + ApiAccountInfoModel.getEmailLink()
    }

    QtObject {
        id: paymentSupport

        readonly property string title: qsTr("Email Billing & Orders")
        readonly property string description: ApiAccountInfoModel.getBillingEmailLink()
        readonly property string link: "mailto:" + ApiAccountInfoModel.getBillingEmailLink()
    }

    QtObject {
        id: site

        readonly property string title: qsTr("Website")
        readonly property string description: ApiAccountInfoModel.getSiteLink()
        readonly property string link: ApiAccountInfoModel.getFullSiteLink()
    }

    property list<QtObject> supportModel: [
        telegram,
        techSupport,
        paymentSupport,
        site
    ]

    ListViewType {
        id: listView

        anchors.fill: parent
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin
        anchors.bottomMargin: 24

        model: supportModel

        header: ColumnLayout {
            width: listView.width

            BackButtonType {
                id: backButton
            }

            BaseHeaderType {
                id: header

                Layout.fillWidth: true
                Layout.rightMargin: 16
                Layout.leftMargin: 16

                headerText: qsTr("Support")
                descriptionText: qsTr("Our technical support specialists are available to assist you at any time")
            }
        }

        delegate: ColumnLayout {
            width: listView.width

            LabelWithButtonType {
                Layout.fillWidth: true
                visible: link !== ""
                text: title
                descriptionText: description
                rightImageSource: "qrc:/images/controls/external-link.svg"
                clickedFunction: function() {
                    Qt.openUrlExternally(link)
                }
            }
            DividerType {}
        }


        footer: ColumnLayout {
            width: listView.width

            // --- Обратная связь из приложения ---
            // Раньше единственным путём был mailto: — на устройстве без почты
            // юзер оставался ни с чем. Теперь текст уходит с gateway.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.topMargin: 16
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Write to us")
                    color: DopamineStyle.color.paleGray
                    font.pixelSize: 16
                    wrapMode: Text.WordWrap
                }

                TextAreaType {
                    id: feedbackText

                    Layout.fillWidth: true
                    Layout.preferredHeight: 110

                    placeholderText: qsTr("What went wrong?")
                }

                SwitcherType {
                    id: attachReportSwitcher

                    Layout.fillWidth: true

                    text: qsTr("Attach connection report")
                    descriptionText: qsTr("Adds diagnostics: your addresses, handshake results, MTU and config. " +
                                          "No browsing history, no traffic contents.")

                    checked: true
                }

                BasicButtonType {
                    id: sendFeedbackButton

                    Layout.fillWidth: true
                    implicitHeight: 40

                    defaultColor: DopamineStyle.color.richBrown
                    hoveredColor: DopamineStyle.color.lightBrown
                    pressedColor: DopamineStyle.color.lightBrown
                    disabledColor: DopamineStyle.color.charcoalGray
                    textColor: DopamineStyle.color.paleGray
                    borderWidth: 0

                    text: ConnectionReportController.feedbackState === "sending"
                          ? qsTr("Sending…") : qsTr("Send feedback")
                    enabled: ConnectionReportController.feedbackState !== "sending"

                    onClicked: {
                        ConnectionReportController.sendFeedback(feedbackText.textAreaText,
                                                                attachReportSwitcher.checked)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: ConnectionReportController.feedbackMessage !== ""
                    text: ConnectionReportController.feedbackMessage
                    color: ConnectionReportController.feedbackState === "error"
                           ? DopamineStyle.color.richBrown : DopamineStyle.color.mutedGray
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }

            DividerType {}

            LabelWithButtonType {
                id: supportUuid
                Layout.fillWidth: true

                text: qsTr("Support tag")
                descriptionText: SettingsController.getInstallationUuid()

                descriptionOnTop: true

                rightImageSource: "qrc:/images/controls/copy.svg"
                rightImageColor: DopamineStyle.color.paleGray

                clickedFunction: function() {
                    GC.copyToClipBoard(descriptionText)
                    PageController.showNotificationMessage(qsTr("Copied"))
                    if (!GC.isMobile()) {
                        this.rightButton.forceActiveFocus()
                    }
                }
            }
        }

        Connections {
            target: ConnectionReportController

            function onReportChanged() {
                // Скан в буфер не копируем: это сотни строк, его место — на сервере.
                if (ConnectionReportController.hasReport && !ConnectionReportController.hasScan) {
                    GC.copyToClipBoard(ConnectionReportController.reportText)
                    PageController.showNotificationMessage(qsTr("Report copied — paste it into the support chat"))
                }
            }
        }
    }
}
