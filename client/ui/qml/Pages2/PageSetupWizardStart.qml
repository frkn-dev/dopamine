import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import PageEnum 1.0
import Style 1.0

import Bip39Helper 1.0

import "./"
import "../Controls2"
import "../Config"
import "../Controls2/TextTypes"
import "../Components"

PageType {
    id: root

    defaultActiveFocusItem: focusItem

    ColumnLayout {
        id: content

        anchors.fill: parent
        spacing: 0

        Item {
            id: focusItem
            KeyNavigation.tab: textKey
        }

        Header2Type {
            Layout.fillWidth: true
            Layout.topMargin: 24
            Layout.bottomMargin: 10
            Layout.rightMargin: 16
            Layout.leftMargin: 16

            headerText: qsTr("Log in FRKN")
        }

        Image {
            id: image
            source: "qrc:/images/amneziaBigLogo.png"

            Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
            Layout.preferredWidth: 180
            Layout.preferredHeight: 144
            Layout.bottomMargin: 10
        }

        LabelTextType {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 10

            text: qsTr("FRKN provides complete anonymity without collecting personal data. Upon registration, you will be provided with a unique 12 words mnemophrase.")
        }

        LabelTextType {
            id: warningText
            Layout.fillWidth: true
            Layout.bottomMargin: 10
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            visible: false

            text: qsTr("● Record and securely save your mnemophrase\n\n● Do not share your mnemophrase with anyone\n\n● In case of loss, recovery is impossible")
        }

        TextFieldWithHeaderType {
            id: textKey

            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 10

            property bool hasValidPhrase: Bip39Helper.validatePhrase(textField.text)

            headerText: qsTr("Your 12 words mnemophrase")
            buttonText: qsTr("Insert")
            textFieldPlaceholderText: "frog roof kitchen nature ..."

            borderColor: textField.text === "" ? AmneziaStyle.color.slateGray : (hasValidPhrase ? AmneziaStyle.color.goldenApricot : AmneziaStyle.color.vibrantRed)
            borderFocusedColor: textField.text === "" ? AmneziaStyle.color.paleGray : (hasValidPhrase ? AmneziaStyle.color.goldenApricot : AmneziaStyle.color.vibrantRed)

            clickedFunc: function() {
                textField.text = ""
                textField.paste()
            }

            KeyNavigation.tab: registerButton.visible ? registerButton : loginButton
        }

        Item
        {
            Layout.fillHeight: true
        }

        BasicButtonType {
            id: copyButton
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 10
            visible: false

            property bool phraseCopied: false

            text: qsTr("Copy mnemonic phrase")

            clickedFunc: function() {
                GC.copyToClipBoard(textKey.textFieldText)
                PageController.showNotificationMessage(qsTr("Copied"))
                phraseCopied = true
                loginButton.forceActiveFocus()
            }

            Keys.onTabPressed: phraseCopied ? loginButton : textKey
        }

        BasicButtonType {
            id: registerButton
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 10

            text: qsTr("Register")

            visible: !textKey.hasValidPhrase

            clickedFunc: function() {
                textKey.buttonText = ""
                textKey.textFieldText = Bip39Helper.generatePhrase()
                textKey.textFieldEditable = false

                visible = false
                copyButton.visible = true
                warningText.visible = true

                PageController.showBusyIndicator(true)

                FrknApi.registerUser(textKey.textFieldText)

                copyButton.forceActiveFocus()
            }

            Keys.onTabPressed: textKey
        }

        BasicButtonType {
            id: loginButton
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.bottomMargin: 48

            enabled: textKey.hasValidPhrase && (!copyButton.visible || copyButton.phraseCopied)

            text: qsTr("Log in")

            clickedFunc: function() {
                FrknApi.loginUser(textKey.textFieldText)
                PageController.showBusyIndicator(true)
            }

            Keys.onTabPressed: copyButton.phraseCopied ? textKey : copyButton
        }
    }

    Connections {
        target: ImportController

        function onImportErrorOccurred(errorMessage, goToPageHome) {
            PageController.showBusyIndicator(false)

            if (goToPageHome) {
                PageController.goToStartPage()
            } else {
                PageController.closePage()
            }
        }

        function onImportFinished() {
            PageController.showBusyIndicator(false)
            ServersModel.processedIndex = ServersModel.defaultIndex
            PageController.goToPageHome()
        }
    }

    Connections {
        target: FrknApi

        function onRegisterFinished(message) {
            console.log("onRegisterFinished:" + message)
            PageController.showBusyIndicator(false)

            if(message === "") {
                // Ok
            } else if(message === "Wrong mnemonic") {
                textKey.textFieldText = Bip39Helper.generatePhrase()
                FrknApi.registerUser(textKey.textFieldText)
            } else {
                textKey.buttonText = ""
                textKey.textFieldText = ""
                textKey.textFieldEditable = true

                registerButton.visible = true
                warningText.visible = false

                PageController.showNotificationMessage(message)
            }
        }

        function onLoginFinished(message, token) {
            console.log("onLoginFinished:" + message + " token:" + token)
            if(message !== "") {
                PageController.showBusyIndicator(false)
                PageController.showNotificationMessage(message)
            }
        }

        function onConnectFinished(message, subscriptionUrl, beta) {
            console.log("onConnectFinished:" + message + " subscriptionUrl:" + subscriptionUrl, " beta:" + beta)
            if(message !== "") {
                PageController.showBusyIndicator(false)
                PageController.showNotificationMessage(message)
            }
        }
    }
}
