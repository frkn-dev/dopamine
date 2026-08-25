import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

import PageEnum 1.0
import QRCodeReader 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

PageType {
    id: root

    // on iOS the camera preview is a native CALayer floating above the whole
    // window — it must be stopped explicitly, page navigation alone doesn't
    // hide it (a page pushed on top would stay covered by the live camera)
    property bool cameraRunning: false

    function startCamera() {
        if (root.cameraRunning) {
            return
        }
        root.cameraRunning = true
        console.log("[QR] camera start")
        qrCodeReader.setCameraSize(Qt.rect(qrCodeRectange.x,
                                           qrCodeRectange.y,
                                           qrCodeRectange.width,
                                           qrCodeRectange.height))
        qrCodeReader.startReading()
    }

    function stopCamera() {
        // not gated by the flag on purpose: a missed start must never leave
        // the native layer hanging — repeated stops are harmless
        root.cameraRunning = false
        console.log("[QR] camera stop")
        qrCodeReader.stopReading()
    }

    Component.onCompleted: root.startCamera()
    Component.onDestruction: root.stopCamera()

    // if a terminal scan did NOT navigate away (fetch failed, malformed link) —
    // resume the camera so the user can retry
    Timer {
        id: resumeCameraTimer
        interval: 1200
        onTriggered: {
            if (root.StackView.status === StackView.Active) {
                root.startCamera()
            }
        }
    }

    // pause the camera while another page covers this one (e.g. the protocol
    // selection pushed after a frkn://sub scan), restart when we're back
    StackView.onStatusChanged: {
        if (StackView.status === StackView.Activating) {
            root.startCamera()
        } else if (StackView.status === StackView.Deactivating) {
            root.stopCamera()
        }
    }

    BackButtonType {
        id: backButton
        anchors.left: parent.left
        anchors.top: parent.top

        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin
    }

    ParagraphTextType {
        id: header

        property string progressString

        anchors.left: parent.left
        anchors.top: backButton.bottom
        anchors.right: parent.right

        anchors.leftMargin: 16
        anchors.rightMargin: 16

        text: qsTr("Point the camera at the QR code and hold for a couple of seconds. ") + progressString
    }

    ProgressBarType {
        id: progressBar

        anchors.left: parent.left
        anchors.top: header.bottom
        anchors.right: parent.right

        anchors.leftMargin: 16
        anchors.rightMargin: 16
    }

    Connections {
        target: ImportController

        function onQrDecodingFinished() {
            PageController.closePage()
        }

        function onSubscriptionConfigsReady(count) {
            PageController.closePage()
        }

        function onSubscriptionErrorOccurred(message) {
            PageController.closePage()
        }

        function onSubscriptionAllDuplicates() {
            PageController.closePage()
        }
    }

    Rectangle {
        id: qrCodeRectange
        anchors.right: parent.right
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.top: progressBar.bottom

        anchors.topMargin: 34
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.bottomMargin: 34

        color: AmneziaStyle.color.transparent
        //radius: 16

        QRCodeReader {
           id: qrCodeReader

           onCodeReaded: function(code) {
               console.log("[QR] code read:", code.length > 40 ? code.substring(0, 40) + "..." : code)
               // any URI is a terminal single-shot code — freeze the camera
               // BEFORE parseQrCodeChunk, it may block on a network fetch
               if (code.indexOf("://") >= 0) {
                   root.stopCamera()
               }
               var done = ImportController.parseQrCodeChunk(code)
               if (done) {
                   // terminal content (frkn:// link, URL, complete chunk set) —
                   // freeze the camera, the flow navigates away on its own
                   root.stopCamera()
                   resumeCameraTimer.restart()
               } else {
                   progressBar.value = ImportController.getQrCodeScanProgressBarValue()
                   header.progressString = ImportController.getQrCodeScanProgressString()
               }
           }
        }
    }
}
