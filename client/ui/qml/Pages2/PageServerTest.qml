import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

import PageEnum 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"
import "../Components"

PageType {
    id: root

    // Pool sections built from ServerTestController.results: rows are grouped by
    // pool, ranked by handshake RTT, and the pool header carries the medians -
    // the same readable shape the status page uses, but with real handshakes.
    property var sections: []

    // Same shape as the server list filters: pool on top, protocol below.
    ListModel { id: poolsModel }
    ListModel { id: protocolsModel }
    property string poolFilter: ServerTestController.poolFilter
    property string protocolFilter: ServerTestController.protocolFilter
    // The subscription selected in the servers list: the pass is limited to it.
    property string subscriptionFilter: ServerTestController.subscriptionFilter

    function rebuildFilters() {
        poolsModel.clear()
        const pools = ServerTestController.availablePools
        for (let i = 0; i < pools.length; ++i) {
            poolsModel.append({ "name": pools[i], "value": pools[i] })
        }

        protocolsModel.clear()
        const protocols = ServerTestController.availableProtocols
        for (let j = 0; j < protocols.length; ++j) {
            protocolsModel.append({ "name": root.protocolLabel(protocols[j]), "value": protocols[j] })
        }
    }

    readonly property bool measuring: ServerTestController.running
    // Temporary diagnostics line: one screenshot then answers "why no protocol /
    // why no speed". Flip to false once the data path is confirmed on devices.
    readonly property bool debugRow: true
    property int diagProtocols: 0
    property int diagSpeeds: 0
    property string diagError: "-" 
    readonly property int measuredCount: ServerTestController.measuredCount
    readonly property int targetCount: ServerTestController.targetCount

    function medianOf(values) {
        if (!values || values.length === 0) {
            return -1
        }
        let copy = values.slice().sort(function(a, b) { return a - b })
        let mid = Math.floor(copy.length / 2)
        return copy.length % 2 === 0 ? (copy[mid - 1] + copy[mid]) / 2 : copy[mid]
    }

    function protocolLabel(tag) {
        if (!tag) {
            return ""
        }
        const lower = tag.toLowerCase()
        if (lower === "awg" || lower === "amneziawg") {
            return "AWG"
        }
        if (lower === "awg-mobile" || lower === "amneziawgmobile") {
            return "AWG Mobile"
        }
        if (lower === "wireguard") {
            return "WG"
        }
        if (lower.indexOf("hysteria") === 0) {
            return "Hysteria2"
        }
        if (lower.indexOf("vless") === 0) {
            return "VLESS"
        }
        return tag
    }

    // Same label shape the server list uses («Subscription 1a2b3c4d»): the two
    // screens must name a subscription alike or the id means nothing.
    function subscriptionLabel(id) {
        const cleaned = (id || "").replace(/-/g, "")
        if (cleaned === "") {
            return qsTr("Subscription")
        }
        const tail = cleaned.length > 8 ? cleaned.slice(cleaned.length - 8) : cleaned
        return qsTr("Subscription %1").arg(tail)
    }

    function speedErrorLabel(code) {
        if (code === "dns") {
            return qsTr("no DNS")
        }
        if (code === "timeout") {
            return qsTr("timeout")
        }
        if (code === "refused") {
            return qsTr("refused")
        }
        if (code === "tls") {
            return qsTr("TLS error")
        }
        if (code === "404" || code === "403" || code === "no_test") {
            return qsTr("no test file")
        }
        if (code === "offline") {
            return qsTr("node offline")
        }
        return qsTr("failed")
    }

    // thresholds mirror status.frkn.org: 4K ~50 Mbps per stream, HD ~10-25,
    // below that it is browsing only; gaming is latency-bound at ~60 ms.
    // Latency and jitter veto a fast verdict: a path that moves 800 Mbps at
    // 527 ms with 1279 ms of jitter cannot hold a video call, so throughput
    // alone must not call it "great".
    function qualityLabel(mbps, rtt, jitter) {
        if (mbps <= 0) {
            return ""
        }
        const jit = Math.max(0, jitter || 0)
        // one step per tier, best first: a high-latency path loses exactly one
        let step
        if (mbps >= 150) {
            step = 0
        } else if (mbps >= 100) {
            step = 1
        } else if (mbps >= 80) {
            step = 2
        } else if (mbps >= 50) {
            step = 3
        } else if (mbps >= 25) {
            step = 4
        } else if (mbps >= 10) {
            step = 5
        } else if (mbps >= 5) {
            step = 6
        } else {
            step = 7
        }
        // unusable for streaming whatever the speed says
        if (rtt > 250 || jit > 200) {
            return qsTr("Unstable: messaging only")
        }
        const highLatency = (rtt >= 150 && rtt <= 250) || (jit >= 80 && jit <= 200)
        if (highLatency) {
            step = Math.min(7, step + 1)
        }
        const verdicts = [
            qsTr("Great for 8K on several devices"),
            qsTr("Great for 8K video"),
            qsTr("Great for 4K on several devices"),
            qsTr("Great for 4K video"),
            qsTr("Good for 4K video"),
            qsTr("Great for HD video"),
            qsTr("Good for HD video"),
            qsTr("Web and messengers only"),
        ]
        let verdict = verdicts[step]
        if (highLatency) {
            verdict += "  ·  " + qsTr("high latency")
        } else if (rtt >= 0 && rtt <= 60 && jit <= 30) {
            verdict += "  ·  " + qsTr("gaming")
        }
        return verdict
    }

    function qualityColor(mbps, rtt, jitter) {
        const jit = Math.max(0, jitter || 0)
        if (rtt > 250 || jit > 200) {
            return "#FF453A"
        }
        if (mbps >= 50) {
            // a fast but laggy path is a warning, not a green light
            return (rtt >= 150 || jit >= 80) ? "#FF9F0A" : "#34C759"
        }
        if (mbps >= 10) {
            return "#FF9F0A"
        }
        return DopamineStyle.color.mutedGray
    }

    function rttColor(rtt) {
        if (rtt < 0) {
            return "#FF453A"
        }
        return rtt < 120 ? "#34C759" : (rtt < 300 ? "#FF9F0A" : "#FF453A")
    }

    function rebuild() {
        // every resultsChanged replaces the model, and a replaced model resets
        // contentY: keep the scroll where the user left it
        const keepY = listView ? listView.contentY : 0
        let rows = ServerTestController.results
        let byPool = ({})
        let order = []

        for (let i = 0; i < rows.length; ++i) {
            let row = rows[i]
            if (!byPool[row.pool]) {
                byPool[row.pool] = { title: row.pool, rows: [] }
                order.push(row.pool)
            }
            byPool[row.pool].rows.push(row)
        }

        order.sort()
        let out = []
        for (let p = 0; p < order.length; ++p) {
            let section = byPool[order[p]]
            section.rows.sort(function(a, b) {
                let ra = a.rtt >= 0 ? a.rtt : 100000 + a.port
                let rb = b.rtt >= 0 ? b.rtt : 100000 + b.port
                return ra - rb
            })

            let rtts = []
            let speeds = []
            for (let k = 0; k < section.rows.length; ++k) {
                let row = section.rows[k]
                if (row.rtt >= 0) {
                    rtts.push(row.rtt)
                }
                if (row.mbps > 0) {
                    speeds.push(row.mbps)
                }
            }
            section.medianRtt = medianOf(rtts)
            section.medianSpeed = medianOf(speeds)
            section.reachable = rtts.length
            section.speedCount = speeds.length
            out.push(section)
        }
        root.sections = out

        let protocolRows = 0
        let speedRows = 0
        let firstError = "-"
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].protocol && rows[i].protocol.length > 0) {
                protocolRows++
            }
            if (rows[i].mbps > 0) {
                speedRows++
            }
            if (firstError === "-" && rows[i].speedError && rows[i].speedError.length > 0) {
                firstError = rows[i].speedError
            }
        }
        root.diagProtocols = protocolRows
        root.diagSpeeds = speedRows
        root.diagError = firstError
        if (listView) {
            Qt.callLater(function() { listView.contentY = keepY })
        }
    }

    Component.onCompleted: {
        ServerTestController.prepareTargets()
        rebuildFilters()
        rebuild()
    }

    Connections {
        target: ServerTestController

        function onResultsChanged() {
            root.rebuild()
        }

        function onFiltersChanged() {
            root.poolFilter = ServerTestController.poolFilter
            root.protocolFilter = ServerTestController.protocolFilter
            root.subscriptionFilter = ServerTestController.subscriptionFilter
            root.rebuildFilters()
        }
    }

    ImageButtonType {
        id: infoButton

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 20 + SettingsController.safeAreaTopMargin
        anchors.rightMargin: 16
        z: 5

        implicitWidth: 48
        implicitHeight: 48

        image: "qrc:/images/controls/info.svg"
        imageColor: DopamineStyle.color.paleGray

        onClicked: PageController.goToPage(PageEnum.PageServerTestInfo)
    }

    Timer {
        id: copyTimer

        interval: 1500
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
        // The Measure button lives in the bar below, not in the scrolling header:
        // the thumb reaches it without scrolling the results back up.
        anchors.bottom: measureBar.top
        anchors.left: parent.left
        anchors.right: parent.right

        ScrollBar.vertical: ScrollBarType { policy: ScrollBar.AlwaysOn }

        header: ColumnLayout {
            width: listView.width

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16

                spacing: 10

                BaseHeaderType {
                    // no fill: the spinner must sit right after the word, not in the
                    // far corner of the row
                    Layout.fillWidth: false
                    Layout.preferredWidth: implicitWidth

                    headerText: qsTr("Speed Test")
                }

                Item {
                    id: spinner

                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter

                    visible: root.measuring
                    opacity: root.measuring ? 1.0 : 0.0

                    // the mascot does not orbit a centre: it drifts up-right, dips
                    // down-left and comes back, flapping, which reads as "working"
                    // without the mechanical look of a spinner ring
                    Image {
                        id: flyer

                        width: 24
                        height: 24
                        anchors.centerIn: parent

                        source: "qrc:/images/pterodactyl.png"
                        fillMode: Image.PreserveAspectFit
                        transformOrigin: Item.Center
                        smooth: true

                        SequentialAnimation {
                            running: root.measuring
                            loops: Animation.Infinite

                            ParallelAnimation {
                                NumberAnimation {
                                    target: flyer
                                    property: "anchors.horizontalCenterOffset"
                                    from: -3
                                    to: 4
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                                NumberAnimation {
                                    target: flyer
                                    property: "anchors.verticalCenterOffset"
                                    from: 3
                                    to: -3
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                                NumberAnimation {
                                    target: flyer
                                    property: "rotation"
                                    from: -10
                                    to: 8
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                            }

                            ParallelAnimation {
                                NumberAnimation {
                                    target: flyer
                                    property: "anchors.horizontalCenterOffset"
                                    from: 4
                                    to: -3
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                                NumberAnimation {
                                    target: flyer
                                    property: "anchors.verticalCenterOffset"
                                    from: -3
                                    to: 3
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                                NumberAnimation {
                                    target: flyer
                                    property: "rotation"
                                    from: 8
                                    to: -10
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                            }

                            // mirrored pass: the same drift to the other side, so the
                            // flight alternates instead of repeating one diagonal
                            ParallelAnimation {
                                NumberAnimation {
                                    target: flyer
                                    property: "anchors.horizontalCenterOffset"
                                    from: -3
                                    to: -4
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                                NumberAnimation {
                                    target: flyer
                                    property: "anchors.verticalCenterOffset"
                                    from: 3
                                    to: -3
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                                NumberAnimation {
                                    target: flyer
                                    property: "rotation"
                                    from: -10
                                    to: -8
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                            }

                            ParallelAnimation {
                                NumberAnimation {
                                    target: flyer
                                    property: "anchors.horizontalCenterOffset"
                                    from: -4
                                    to: -3
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                                NumberAnimation {
                                    target: flyer
                                    property: "anchors.verticalCenterOffset"
                                    from: -3
                                    to: 3
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                                NumberAnimation {
                                    target: flyer
                                    property: "rotation"
                                    from: -8
                                    to: -10
                                    duration: 460
                                    easing.type: Easing.InOutSine
                                }
                            }
                        }

                        // wing flap: a short vertical squash on top of the drift
                        SequentialAnimation on scale {
                            running: root.measuring
                            loops: Animation.Infinite

                            NumberAnimation {
                                target: flyer
                                property: "scale"
                                from: 1.0
                                to: 0.88
                                duration: 230
                                easing.type: Easing.InOutQuad
                            }
                            NumberAnimation {
                                target: flyer
                                property: "scale"
                                from: 0.88
                                to: 1.0
                                duration: 230
                                easing.type: Easing.InOutQuad
                            }
                        }
                    }
                }
            }

            // Scope of the pass: the subscription picked in the servers list.
            // Shown here so nobody has to guess why a pool is (or is not) on
            // the list, and with "all" every row names its own subscription.
            CaptionTextType {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8

                text: {
                    if (root.subscriptionFilter === "") {
                        return qsTr("All subscriptions — pick one in the server list to test only it")
                    }
                    if (root.subscriptionFilter === "__other__") {
                        return qsTr("Servers outside subscriptions")
                    }
                    return root.subscriptionLabel(root.subscriptionFilter)
                }
                color: DopamineStyle.color.mutedGray
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }

            FilterDropDown {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8

                visible: poolsModel.count > 0
                filterModel: poolsModel
                currentValue: root.poolFilter

                onSelected: function(value) {
                    root.poolFilter = value
                    root.sections = []  // clear now, not when the C++ side answers
                    ServerTestController.setPoolFilter(value)
                }
            }

            // The pool's protocols, all of them — including the ones no card of
            // the pool was provisioned for yet, because picking one prepares it
            // and tests it. A single protocol is not a choice: the chooser is not
            // rendered at all then (ServerTestController.hasProtocolChoice).
            FilterDropDown {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 8

                visible: ServerTestController.hasProtocolChoice
                filterModel: protocolsModel
                currentValue: root.protocolFilter

                onSelected: function(value) {
                    root.protocolFilter = value
                    ServerTestController.selectProtocol(value)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 4

                spacing: 8

                visible: root.measuring

                CaptionTextType {
                    Layout.fillWidth: true

                    text: qsTr("%1 of %2 measured").arg(ServerTestController.measuredCount).arg(ServerTestController.targetCount)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 4

                spacing: 8

            CaptionTextType {
                Layout.fillWidth: true

                text: {
                    if (root.targetCount === 0) {
                        return qsTr("This subscription has no pooled nodes to test")
                    }
                    if (ServerTestController.status === "provisioning") {
                        return qsTr("Preparing the protocol, then the test starts")
                    }
                    if (ServerTestController.status === "waitingForTunnelOff") {
                        return qsTr("Waiting for the VPN to go off, then the test starts by itself")
                    }
                    if (ServerTestController.status === "stoppedTunnel") {
                        return qsTr("Stopped: the tunnel came up, so speed was not measured - disconnect and measure again")
                    }
                    if (ConnectionController.isConnected) {
                        return qsTr("Disconnect first: with the tunnel up every probe goes through the exit")
                    }
                    return qsTr("Measured %1 of %2 addresses").arg(root.measuredCount).arg(root.targetCount)
                }
                color: ServerTestController.status === "stoppedTunnel" ? "#FF9F0A" : DopamineStyle.color.midnightBlack
            }

                ImageButtonType {
                    id: copyButton

                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 44

                    visible: ServerTestController.measuredCount > 0
                    opacity: copyTimer.running ? 0.5 : 1.0

                    image: "qrc:/images/controls/copy.svg"
                    imageColor: copyTimer.running ? DopamineStyle.color.goldenApricot : DopamineStyle.color.paleGray

                    onClicked: {
                        GlobalConfig.copyToClipBoard(ServerTestController.reportJson())
                        copyTimer.restart()
                    }
                }
            }

            DividerType { Layout.fillWidth: true }
        }

        model: root.sections

        delegate: ColumnLayout {
            width: listView.width
            spacing: 0

            required property var modelData

            Repeater {
                model: modelData.rows

                delegate: RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 16
                    Layout.rightMargin: 16
                    Layout.topMargin: 6
                    Layout.bottomMargin: 6

                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        ParagraphTextType {
                            Layout.fillWidth: true

                            // both names: the one from the status page and the one the
                            // server list shows, so the user can find that server there
                            text: {
                                const base = modelData.label.length === 0 ? modelData.hostname : modelData.label
                                const listed = modelData.listName || ""
                                if (listed.length === 0 || listed === base) {
                                    return base
                                }
                                return base + "  ·  " + listed
                            }
                            color: DopamineStyle.color.paleGray
                            elide: Text.ElideRight
                        }

                        // Only with "all subscriptions": with one subscription
                        // selected every row comes from it, so the line would
                        // repeat the same id on every card.
                        CaptionTextType {
                            Layout.fillWidth: true

                            visible: text.length > 0
                            text: (modelData.subscription || "").length > 0
                                  ? qsTr("Subscription %1").arg(modelData.subscription)
                                  : ""
                            color: DopamineStyle.color.mutedGray
                            elide: Text.ElideRight
                            wrapMode: Text.NoWrap
                        }

                    }

                    ColumnLayout {
                        spacing: 0

                        ParagraphTextType {
                            // A proxy protocol has no handshake to show: the
                            // column says so in muted text instead of a red "no
                            // answer" the node never owed.
                            text: {
                                if (modelData.speedOnly === true) {
                                    return qsTr("speed only")
                                }
                                if (modelData.rtt === -2) {
                                    return "—"
                                }
                                if (modelData.rtt < 0) {
                                    return qsTr("no answer")
                                }
                                return qsTr("%1 ms").arg(modelData.rtt)
                            }
                            color: modelData.speedOnly === true || modelData.rtt === -2
                                   ? DopamineStyle.color.mutedGray
                                   : (modelData.rtt < 0 ? "#FF453A" : root.rttColor(modelData.rtt))
                        }

                        CaptionTextType {
                            text: {
                                if (modelData.speedOnly === true) {
                                    return root.protocolLabel(modelData.protocol)
                                }
                                if (modelData.rtt === -2) {
                                    return ""
                                }
                                if (modelData.rtt < 0) {
                                    return qsTr("lost %1 of %2").arg(modelData.samples).arg(modelData.samples)
                                }
                                if (modelData.loss > 0) {
                                    return qsTr("lost %1 of %2").arg(modelData.loss).arg(modelData.samples)
                                }
                                return modelData.jitter > 0 ? qsTr("jitter %1 ms").arg(modelData.jitter) : qsTr("stable")
                            }
                            color: DopamineStyle.color.mutedGray
                        }
                    }

                    ColumnLayout {
                        spacing: 0

                        ParagraphTextType {
                            Layout.alignment: Qt.AlignRight

                            text: modelData.mbps > 0 ? modelData.mbps.toFixed(1) + " Mbps" : "—"
                            color: modelData.mbps > 0 ? DopamineStyle.color.paleGray : DopamineStyle.color.mutedGray
                        }

                        CaptionTextType {
                            Layout.alignment: Qt.AlignRight

                            text: modelData.mbps > 0
                                  ? root.qualityLabel(modelData.mbps, modelData.rtt, modelData.jitter)
                                  : (modelData.speedErrorCode.length > 0
                                     ? root.speedErrorLabel(modelData.speedErrorCode)
                                     : (modelData.speedError.length > 0 ? qsTr("not measured") : ""))
                            color: modelData.mbps > 0
                                   ? root.qualityColor(modelData.mbps, modelData.rtt, modelData.jitter)
                                   : DopamineStyle.color.mutedGray
                        }
                    }
                }
            }

            DividerType { Layout.fillWidth: true; Layout.topMargin: 6 }
        }
    }

    // The Measure button sits at the bottom of the page, outside the scrolling
    // list: one-handed reach, and it never scrolls away while results are read.
    // Side margins 16 px, height 56 px (>= 52), bottom margin above the home
    // indicator, and the bar owns the list's bottom edge so no row hides under it.
    Rectangle {
        id: measureBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        height: measureButton.implicitHeight + 16 + SettingsController.safeAreaBottomMargin

        z: 3
        // opaque: results scrolling underneath must not show through the bar
        color: DopamineStyle.color.midnightBlack

        DividerType {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
        }

        BasicButtonType {
            id: measureButton

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.bottomMargin: 8 + SettingsController.safeAreaBottomMargin

            implicitHeight: 56

            text: {
                if (root.measuring) {
                    return qsTr("Stop")
                }
                if (ConnectionController.isConnected) {
                    return qsTr("Disconnect and measure")
                }
                return qsTr("Measure")
            }

            clickedFunc: function() {
                if (ServerTestController.running) {
                    ServerTestController.stop()
                    return
                }
                if (ConnectionController.isConnected) {
                    // disconnect first: with the tunnel up every probe goes
                    // through the exit and the speed numbers describe the exit
                    ConnectionController.connectButtonClicked()
                }
                ServerTestController.runWhenTunnelOff(true)
            }
        }
    }
}
