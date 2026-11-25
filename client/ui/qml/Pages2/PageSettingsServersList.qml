import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import SortFilterProxyModel 0.2

import PageEnum 1.0
import ProtocolEnum 1.0
import ContainerProps 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    property bool subscriptionReloadStartedHere: false
    // v2 peers reload runs in parallel with the legacy api-configs reload. Both
    // parts must finish before the spinner stops and exactly ONE notification
    // appears — otherwise the mascot flaps twice (one flight per loader) and two
    // "reloaded" bubbles stack up.
    property bool peersReloadPending: false
    property int reloadPendingParts: 0
    property bool reloadAnyFailed: false
    // Cards of the shard arrived during this reload: the list IS updated, so a
    // failure of the legacy v1 part must not shout "Failed to reload servers"
    // over the "Servers loaded" flight.
    property bool reloadServersLanded: false
    property bool reloadInFlight: false
    // What the reload button is about to refresh, for the result notification.
    property string pendingReloadLabel: ""

    // Neither loader can be trusted to always answer: on Android the legacy
    // api-configs request has been seen finishing with code 0 and an empty body
    // (QNetworkReply timeout -> fallback endpoint), which left the counter armed
    // forever and the spinner flying with no way out. A stuck reload is a failed
    // reload: cut it loose instead of freezing the screen.
    Timer {
        id: reloadWatchdog

        interval: 25000
        repeat: false

        onTriggered: {
            if (!root.reloadInFlight) {
                return
            }
            console.warn("[RELOAD] watchdog fired: parts still pending", root.reloadPendingParts)
            root.reloadPendingParts = 0
            root.peersReloadPending = false
            root.finishReloadPart(false)
        }
    }

    function finishReloadPart(ok) {
        // A late part (a loader that answers after the others already drained the
        // counter) must not produce a second notification: the reload is over.
        if (!root.reloadInFlight) {
            return
        }
        if (!ok) {
            root.reloadAnyFailed = true
        }
        if (root.reloadPendingParts > 0) {
            root.reloadPendingParts -= 1
        }
        if (root.reloadPendingParts > 0) {
            return
        }
        root.reloadInFlight = false
        reloadWatchdog.stop()
        PageController.busyOff()
        const scope = root.pendingReloadLabel
        root.pendingReloadLabel = ""
        const failed = root.reloadAnyFailed && !root.reloadServersLanded
        root.reloadAnyFailed = false
        root.reloadServersLanded = false
        if (failed) {
            PageController.showNotificationMessage(qsTr("Failed to reload servers"))
        } else {
            // one fly-by for a successful reload: the mascot is the confirmation,
            // and no text bubble follows it (it used to fire per loader)
            PageController.notifyServersLoaded()
        }
        // Success is silent on purpose: the list itself is the confirmation, and
        // a toast under it only duplicates the message the user already sees.
    }

    Connections {
        target: PeersImportController

        function onCardsReady(count) {
            if (!root.peersReloadPending) {
                return
            }
            if (count > 0) {
                root.reloadServersLanded = true
            }
            root.peersReloadPending = false
            root.finishReloadPart(true)
        }

        function onCardsFailed(httpStatus, error) {
            if (!root.peersReloadPending) {
                return
            }
            root.peersReloadPending = false
            root.finishReloadPart(false)
        }
    }

    Connections {
        target: ApiConfigsController

        function onReloadSubscriptionConfigsFinished(success) {
            if (!root.subscriptionReloadStartedHere) {
                return
            }
            root.subscriptionReloadStartedHere = false
            root.finishReloadPart(success)
        }
    }

    RowLayout {
        id: header

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin

        spacing: 0

        BackButtonType {
            id: backButton
        }

        // No title here: the list itself says what it is, and the freed space
        // goes to the action buttons (help, measure ping, reload, add).
        Item {
            Layout.fillWidth: true
            Layout.leftMargin: 16
        }

        ImageButtonType {
            id: helpButton

            Layout.rightMargin: 4

            // The same explainer Settings links to: what this list is, what the
            // protocols mean and where the traffic ends up.
            visible: true

            image: "qrc:/images/controls/info.svg"
            imageColor: DopamineStyle.color.paleGray

            implicitWidth: 48
            implicitHeight: 48

            onClicked: PageController.goToPage(PageEnum.PageHowItWorks)
        }

        ImageButtonType {
            id: measurePingButton

            Layout.rightMargin: 4

            image: "qrc:/images/controls/gauge.svg"
            imageColor: DopamineStyle.color.paleGray

            implicitWidth: 48
            implicitHeight: 48

            onClicked: HealthCheckController.startProbe(true)
        }

        ImageButtonType {
            id: reloadFromApiButton

            Layout.rightMargin: 4

            // Always available: the list can be empty for reasons the button is
            // the cure for (a grant with no card protocols, a wiped list), and a
            // hidden reload left the user with no way back.
            visible: true

            image: "qrc:/images/controls/refresh-cw.svg"
            imageColor: DopamineStyle.color.paleGray

            implicitWidth: 48
            implicitHeight: 48

            onClicked: {
                // "All" reloads everything; a selected subscription reloads only
                // itself. The "Other" bucket has no subscription id of its own, so
                // it falls back to a full reload.
                // One reload at a time: a second tap while parts are still in
                // flight would otherwise start another round and add its own
                // bubble on top of the first.
                if (root.reloadInFlight) {
                    return
                }
                const selected = servers.subscriptionFilter
                const scoped = selected === "" || selected === servers.otherSubscriptionValue
                const subIds = ServersModel.availableSubscriptions
                const peersPart = scoped ? (subIds.length > 0 ? 1 : 0) : 1
                root.pendingReloadLabel = scoped ? "" : servers.subscriptionLabel(selected)
                root.reloadAnyFailed = false
                root.reloadServersLanded = false
                root.reloadPendingParts = 1 + peersPart
                root.reloadInFlight = true
                reloadWatchdog.restart()
                PageController.busyOn()
                root.subscriptionReloadStartedHere = true
                ApiConfigsController.reloadSubscriptionConfigs(false, scoped ? "" : selected)
                // v2: the pools that live in the shard (ru/us/kz) come from
                // /v1/locations, not from the legacy api-configs endpoint.
                if (!scoped) {
                    root.peersReloadPending = true
                    PeersImportController.refreshCards(selected, true)
                } else {
                    // "All" re-reads every subscription that has pool rows. The
                    // legacy reload above only speaks for the v1 subscription;
                    // without this the pools of a second (or v2-only) install
                    // were never re-read and kept the row an older build wrote
                    // — one protocol, no variant list to switch with.
                    const ids = ServersModel.availableSubscriptions
                    root.peersReloadPending = ids.length > 0
                    for (let i = 0; i < ids.length; ++i) {
                        PeersImportController.refreshCards(ids[i], true)
                    }
                }
            }
        }

        ImageButtonType {
            id: addServerButton

            Layout.rightMargin: 12

            image: "qrc:/images/controls/plus.svg"
            imageColor: DopamineStyle.color.paleGray

            implicitWidth: 48
            implicitHeight: 48

            onClicked: PageController.goToPage(PageEnum.PageSetupWizardConfigSource)
        }
    }

    ServersListView {
        id: servers

        objectName: "servers"

        // A server was picked and the dial already started in the controller —
        // take the user back to the main screen instead of leaving him on a list
        // that no longer has anything to confirm.
        onServerSelected: PageController.goToPage(PageEnum.PageHome)
        anchors.top: header.bottom
        anchors.topMargin: 16
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
    }
}
