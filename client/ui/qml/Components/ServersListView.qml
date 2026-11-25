import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects

import PageEnum 1.0
import ProtocolEnum 1.0
import ContainerProps 1.0
import ContainersModelFilters 1.0
import Style 1.0

import "./"
import "../Controls2"
import "../Controls2/TextTypes"
import "../Config"

ListViewType {
    id: root

    // A tap on a server connects right away: no extra Connect button, no
    // "go back to the main screen first". Switching while a tunnel is up goes
    // through the controller's switch path (stop, then dial the new card);
    // tapping the already-selected row does not restart the tunnel.
    signal serverSelected()

    // Protocol pill: switch the row's protocol, re-measure it, and — if the row
    // is the one currently connected — re-dial, so the tunnel actually moves to
    // the protocol the user just picked. Handled in the root scope because the
    // singleton is not resolvable inside the nested delegate.
    function protocolPillSelected(sourceIndex, cardId) {
        if (!ServersModel.setPeerPoolProtocol(sourceIndex, cardId)) {
            return
        }
        HealthCheckController.startProbe(true)
        if (ConnectionController.isConnected && root.selectedIndex === sourceIndex) {
            ConnectionController.openConnection()
        }
        // Same contract as tapping the row: the choice is made, go back home.
        root.serverSelected()
    }

    function tapServerRow(sourceIndex) {
        SettingsController.autoServerSelection = false
        const previousRow = root.selectedIndex
        root.selectedIndex = sourceIndex
        ServersModel.defaultIndex = sourceIndex
        if (!ConnectionController.isConnected || previousRow !== sourceIndex) {
            ConnectionController.openConnection()
        }
        root.serverSelected()
    }

    property int selectedIndex: ServersModel.defaultIndex
    property string subscriptionFilter: ""
    // sentinel for servers that do not belong to any FRKN subscription
    readonly property string otherSubscriptionValue: "__other__"
    property var expandedGroups: ({})
    // Variant lists live here, not in the group model: ListModel turns an array
    // of objects into a nested ListModel, so `group.variants[0]` is undefined and
    // a Repeater bound to it draws empty rows. A plain JS array read from this
    // map keeps giving real objects to `modelData`.
    property var groupVariantsByKey: ({})

    ButtonGroup {
        id: serversRadioButtonGroup
    }

    Component.onCompleted: {
        // The env/protocol selectors are gone from the list, but the connect path
        // (auto-select and its fallback iteration) still reads the persisted env
        // filter: an old non-"all" value would silently keep limiting it.
        SettingsController.serversEnvFilter = "all"
        const savedSub = SettingsController.serversSubscriptionFilter
        if (savedSub !== "" && savedSub !== "all") {
            root.subscriptionFilter = savedSub
        }
        rebuildSubscriptionsModel()
        rebuildGroups()

        if (!ConnectionController.isConnected) {
            HealthCheckController.startProbe()
        }
    }

    function flagCountryCode(cc) {
        const aliases = {
            "SWE": "SE", "HEL": "FI", "UK": "GB",
            "EST": "EE", "TLL": "EE", "EESTI": "EE", "ESTONIA": "EE", "TALLINN": "EE"
        }
        return aliases[cc] !== undefined ? aliases[cc] : cc
    }

    function isElephant(variant) {
        if (variant.protocol === "vlessxhttpcdn") {
            return true
        }
        const haystack = ((variant.name || "") + " " + (variant.groupName || "") + " " + (variant.pool || "")).toLowerCase()
        return haystack.indexOf("belyslon") >= 0
                || haystack.indexOf("bely slon") >= 0
                || haystack.indexOf("белый слон") >= 0
                || haystack.indexOf("білий слон") >= 0
    }

    function protocolLabel(proto) {
        if (proto === "") return ""
        if (proto === "vless") return "VLESS"
        if (proto === "hysteria2") return "Hysteria2"
        if (proto === "awg") return "AmneziaWG"
        if (proto === "awg-mobile" || proto === "amneziawgmobile") return "AmneziaWG Mobile"
        if (proto === "wireguard") return "WireGuard"
        return proto
    }

    // The tail of the subscription id, not a sequence number: "Subscription 1/2"
    // said nothing about which subscription was which.
    function subscriptionLabel(id) {
        const cleaned = (id || "").replace(/-/g, "")
        if (cleaned === "") {
            return qsTr("Subscription")
        }
        const tail = cleaned.length > 8 ? cleaned.slice(cleaned.length - 8) : cleaned
        return qsTr("Subscription %1").arg(tail)
    }

    // `<shard>:<pool>` of a peers card id (`<shard>:<pool>:<proto>`). Rows
    // written before `peerPoolKey` existed have no pool key of their own; the
    // card id still identifies their pool, so the list can keep them in one row
    // per pool instead of opening a second "Russia" beside the real one.
    function peerPoolKeyOf(cardId) {
        const raw = (cardId || "").trim()
        const sep = raw.lastIndexOf(":")
        return sep > 0 ? raw.slice(0, sep) : ""
    }

    function protocolRank(proto) {
        const p = (proto || "").toLowerCase()
        if (p === "awg-mobile" || p === "amneziawgmobile") return 0
        if (p === "awg") return 1
        if (p === "vless") return 2
        if (p === "wireguard") return 3
        // hysteria2 runs on its own transport (xray) and is ranked after the
        // WG family, so a pool that serves it keeps the WG protocol in front.
        if (p === "hysteria2") return 4
        return 5
    }

    // A different subscription (or the "Other" bucket / none) means different
    // rows: only the list itself has to be rebuilt.
    onSubscriptionFilterChanged: {
        rebuildGroups()
    }

    // servers that are not part of any FRKN subscription (share links, imported
    // configs) — shown as a separate "Other" bucket so they don't vanish when a
    // concrete subscription is selected
    function hasNonSubscriptionServers() {
        const subs = ServersModel.availableSubscriptions
        for (let i = 0; i < ServersModel.getServersCount(); ++i) {
            const variant = ServersModel.getServerDisplayData(i)
            const id = variant.subscriptionId || ""
            if (id === "" || subs.indexOf(id) < 0) {
                return true
            }
        }
        return false
    }

    function rebuildSubscriptionsModel() {
        const subs = ServersModel.availableSubscriptions
        if (root.subscriptionFilter !== "" && root.subscriptionFilter !== "all"
                && root.subscriptionFilter !== root.otherSubscriptionValue
                && subs.indexOf(root.subscriptionFilter) < 0) {
            root.subscriptionFilter = ""
        }

        subscriptionsModel.clear()
        subscriptionsModel.append({ "name": qsTr("All"), "value": "" })
        for (let i = 0; i < subs.length; i++) {
            subscriptionsModel.append({ "name": subscriptionLabel(subs[i]), "value": subs[i], "deletable": true })
        }
        if (hasNonSubscriptionServers()) {
            subscriptionsModel.append({ "name": qsTr("Other"), "value": root.otherSubscriptionValue })
        }
    }

    ListModel {
        id: subscriptionsModel
    }

    // Row selection is the subscription filter alone: the env/shard and protocol
    // selectors left the list, so every row of the chosen subscription (or of
    // every subscription) is in play.
    function variantMatches(variant) {
        const sub = root.subscriptionFilter
        if (sub !== "" && sub !== "all") {
            const id = variant.subscriptionId || ""
            if (sub === root.otherSubscriptionValue) {
                if (ServersModel.availableSubscriptions.indexOf(id) >= 0) {
                    return false
                }
            } else if (id !== sub) {
                return false
            }
        }
        return true
    }

    // Which variant rows of a group are on screen right now.
    //  - a peers pool: exactly one row (see below)
    //  - expanded: every variant (the user asked for the full list)
    //  - collapsed: one row per distinct protocol (legacy v1 configs), and when
    //    the user picked a row of that protocol, that row is the one shown.
    // The split is on the RAW backend tag, never on the canonical one:
    // `awg-mobile` is wire-compatible with `awg` (both canonicalize to `awg`),
    // so keying on the canonical value folded the AmneziaWgMobile row into the
    // plain AmneziaWG one — the mobile row existed in the model but was never
    // drawn, and LTE users had no row to dial.
    function protocolKeyOf(variant) {
        if (!variant) return ""
        return ((variant.protocolRaw || variant.protocol) || "").toLowerCase()
    }

    // Canonical protocol of a raw tag (mirrors canonicalServiceProtocol in C++):
    // wire-compatible variants share the base protocol for logic.
    function canonicalProtocolKey(raw) {
        const p = (raw || "").toLowerCase()
        if (p === "awg-mobile" || p === "amneziawgmobile") return "awg"
        return p
    }

    // Canonical protocols every peers row of a subscription serves, keyed by
    // subscription. A v2 subscription produces two row sources at once: the
    // pooled peer rows (`peerCardId` set, `peerVariants` carrying the cards the
    // pills switch to) and the legacy per-protocol rows built from the
    // subscription's v1 connections. Both are here, in one list, for the same
    // subscription — hence the second card beside a pool that already shows the
    // protocol as a pill.
    function pooledProtocolsBySubscription(variants) {
        const bySubscription = Object.create(null)
        for (let i = 0; i < variants.length; ++i) {
            const variant = variants[i]
            if (!variant || !variant.peerCardId) continue
            const sub = variant.peerSubscriptionId || variant.subscriptionId || ""
            if (sub === "") continue
            if (!bySubscription[sub]) {
                bySubscription[sub] = Object.create(null)
            }
            // The row's own protocol is a variant of the pool too: a pool row
            // written before `peerVariants` existed still names the protocol it
            // dials.
            const own = root.canonicalProtocolKey(root.protocolKeyOf(variant))
            if (own !== "") bySubscription[sub][own] = true
            const poolVariants = variant.peerVariants || []
            for (let j = 0; j < poolVariants.length; ++j) {
                const entry = poolVariants[j]
                if (!entry) continue
                // `peerVariants[].protocol` is already canonical (C++
                // getServerDisplayData canonicalizes it); canonicalize again so a
                // raw tag never slips past the match.
                const proto = root.canonicalProtocolKey(entry.protocol || "")
                if (proto !== "") bySubscription[sub][proto] = true
            }
        }
        return bySubscription
    }

    // A legacy per-protocol row is a duplicate card when the same subscription
    // already has a pooled row whose pool serves that protocol: the pills are the
    // only protocol switch for a pooled pool. Rows of subscriptions without
    // pooled rows (every v1 subscription, including the AmneziaWG +
    // AmneziaWG Mobile pair) are untouched.
    // Computed from the model on every rebuild and never cached on the row: when
    // the pooled row disappears (no grant), the legacy row is the only way to
    // connect and must come back.
    function isShadowedByPool(variant, pooledBySubscription) {
        if (!variant || variant.peerCardId) {
            return false
        }
        const sub = variant.subscriptionId || ""
        if (sub === "" || !pooledBySubscription[sub]) {
            return false
        }
        const proto = root.canonicalProtocolKey(root.protocolKeyOf(variant))
        return proto !== "" && pooledBySubscription[sub][proto] === true
    }

    // Title of one row. Both Amnezia rows of a v1 pool carry the same pool name,
    // and the protocol subtitle is small: the wire-compatible variant names
    // itself in the title ("… (AmneziaWG Mobile)") so the LTE row cannot be
    // mistaken for the plain one. Other rows keep their plain name, and a peers
    // pool is untouched — its protocol is a pill on the row.
    // The title is computed once per rebuild and stored on the variant: the row
    // delegate must not depend on the group it sits in.
    function applyVariantTitles(variants) {
        for (let i = 0; i < variants.length; ++i) {
            const variant = variants[i]
            if (!variant) continue
            variant.rowTitle = variant.name
            if (variant.isPeerPool) continue
            const raw = root.protocolKeyOf(variant)
            const canonical = root.canonicalProtocolKey(raw)
            if (raw === "" || raw === canonical) continue
            let plainSibling = false
            for (let j = 0; j < variants.length; ++j) {
                if (j !== i && root.protocolKeyOf(variants[j]) === canonical) {
                    plainSibling = true
                    break
                }
            }
            if (plainSibling) {
                const label = root.protocolLabel(raw)
                variant.rowTitle = (variant.name !== "" ? variant.name : label) + " (" + label + ")"
            }
        }
    }
    // Visibility of one row of a group. A legacy row that duplicates a pooled
    // row's protocol never reaches this function: the group builder drops it
    // (see isShadowedByPool), so the cards, the counts and the badges stay in
    // step. Everything below is about the rows that are left.
    function cardIsVisible(group, index) {
        const variants = root.groupVariants(group)
        if (index < 0 || index >= variants.length) {
            return false
        }
        const variant = variants[index]
        if (!variant) {
            return false
        }
        // A peers pool is one row: its protocol is a property of the row. Drawing
        // one card per protocol is exactly the duplicate ("Russia" twice: AWG +
        // WG) this grouping replaces.
        if (variant.isPeerPool) {
            // One row per pool even if a stale single-card row of the same pool
            // is still around: the pooled row (the one that knows the pool's
            // protocols) is drawn, not whichever sorted first.
            for (let i = 0; i < variants.length; ++i) {
                const other = variants[i]
                if (other && other.isPeerPool && other.peerVariants !== undefined
                        && other.peerVariants.length > 0) {
                    return index === i
                }
            }
            return index === 0
        }
        if (group.expanded) {
            return true
        }
        const protocol = root.protocolKeyOf(variant)
        let chosen = -1
        for (let i = 0; i < variants.length; ++i) {
            const other = variants[i]
            if (!other || root.protocolKeyOf(other) !== protocol) {
                continue
            }
            if (chosen < 0) {
                chosen = i
            }
            if (other.sourceIndex === root.selectedIndex) {
                chosen = i
            }
        }
        return index === chosen
    }

    function visibleCardCount(group) {
        const variants = root.groupVariants(group)
        if (!variants.length) {
            return 0
        }
        let count = 0
        for (let i = 0; i < variants.length; ++i) {
            if (cardIsVisible(group, i)) {
                count++
            }
        }
        return count
    }

    function rebuildGroups() {
        const previousExpanded = root.expandedGroups
        const expanded = Object.assign({}, previousExpanded)
        const byKey = Object.create(null)

        // The whole rebuild starts from the model, so a vanished pool row (no
        // grant) is gone from this pass too and its legacy rows come back.
        const matched = []
        for (let i = 0; i < ServersModel.getServersCount(); ++i) {
            const variant = ServersModel.getServerDisplayData(i)
            if (!variantMatches(variant)) continue
            matched.push(variant)
        }
        // Which protocols the subscriptions' pools already switch with a pill.
        // Rows are shadowed here, once, so the cards, the group counts and the
        // header badges all read the same array. The rows stay in the model.
        const pooledBySubscription = root.pooledProtocolsBySubscription(matched)

        for (const variant of matched) {
            if (root.isShadowedByPool(variant, pooledBySubscription)) continue

            const pool = (variant.pool || "").trim()
            const title = variant.groupName || variant.name || variant.countryName || "FRKN"
            // Two origins, two key spaces. A v2 peers row is one (env, pool) — its
            // `poolKey` is `<shard>:<pool>` — and it must never be merged with a
            // legacy v1 config that happens to carry the same pool name.
            // The pool identity falls back to the card id (`<shard>:<pool>:<proto>`
            // minus the protocol segment) so a row written before `poolKey`
            // existed still lands in its pool's group instead of opening a second
            // "Russia" next to it.
            const key = variant.isPeerPool
                    ? "peers|" + (variant.subscriptionId || "") + "|"
                      + (variant.poolKey || peerPoolKeyOf(variant.peerCardId) || pool || title)
                    : "v1|" + (variant.subscriptionId || "") + "|" + (pool || title)
                      + (variant.subscriptionId ? "" : "|" + (variant.connectionUuid || i))

            if (!byKey[key]) {
                byKey[key] = {
                    key: key,
                    title: title,
                    isPool: pool !== "",
                    variants: [],
                    expanded: expanded[key] === true
                }
            }
            if (pool !== "") {
                byKey[key].isPool = true
            }
            byKey[key].variants.push(variant)
        }

        const groups = []
        for (const key of Object.keys(byKey)) {
            const group = byKey[key]
            // Ranked by the raw protocol tag: AmneziaWgMobile outranks plain
            // AmneziaWG (and has to, or the mobile row sorts below it).
            group.variants.sort(function(a, b) {
                return protocolRank(root.protocolKeyOf(a)) - protocolRank(root.protocolKeyOf(b)) || a.sourceIndex - b.sourceIndex
            })
            group.preferred = group.variants[0]
            for (const variant of group.variants) {
                if (variant.sourceIndex === root.selectedIndex) {
                    group.preferred = variant
                    break
                }
            }
            root.applyVariantTitles(group.variants)
            groups.push(group)
        }

        // Pool cards come first: they are what the user starts from. Inside a
        // group the rest follow alphabetically.
        groups.sort(function(a, b) {
            if (a.isPool !== b.isPool) {
                return a.isPool ? -1 : 1
            }
            return a.title.localeCompare(b.title) || a.key.localeCompare(b.key)
        })

        groupsModel.clear()
        const variantsByKey = Object.create(null)
        for (const group of groups) {
            groupsModel.append(group)
            variantsByKey[group.key] = group.variants
        }
        root.groupVariantsByKey = variantsByKey
        root.expandedGroups = expanded
    }

    // Variants of a group as a plain JS array (see groupVariantsByKey).
    function groupVariants(group) {
        if (!group) {
            return []
        }
        const list = root.groupVariantsByKey[group.key]
        if (list && list.length) {
            return list
        }
        return group.preferred ? [group.preferred] : []
    }

    // A v2 peers pool is one row: its group exists only to keep the row in the
    // flat order, so the delegate must draw no group header (title, «N×» badge,
    // «+» expander) for it. The protocol is named on the row and switched in
    // the server card the gear opens.
    function groupIsPeerPool(group) {
        const variants = root.groupVariants(group)
        for (let i = 0; i < variants.length; ++i) {
            if (variants[i] && variants[i].isPeerPool) {
                return true
            }
        }
        return false
    }

    function toggleGroup(key) {
        const expanded = Object.assign({}, root.expandedGroups)
        expanded[key] = expanded[key] !== true
        root.expandedGroups = expanded
        rebuildGroups()
    }

    ListModel {
        id: groupsModel
    }

    header: Item {
        visible: true
        width: root.width
        height: visible ? headerColumn.implicitHeight + 8 : 0

        ColumnLayout {
            id: headerColumn

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 8

            RowLayout {
                id: subscriptionFilterRow

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                visible: ServersModel.availableSubscriptions.length > 1

                spacing: 8

                FilterDropDown {
                    id: subscriptionFilterDropDown

                    Layout.fillWidth: true

                    filterModel: subscriptionsModel
                    currentValue: root.subscriptionFilter
                    deletableValues: true

                    onSelected: function(value) {
                        root.subscriptionFilter = value
                        SettingsController.serversSubscriptionFilter = value === "" ? "all" : value
                        root.rebuildGroups()
                    }

                    onDeleteRequested: function(value) {
                        const label = root.subscriptionLabel(value)
                        showQuestionDrawer(qsTr("Delete %1?").arg(label),
                                           qsTr("Servers of this subscription are removed from this device and their tunnels are released on the server, so the device keeps its tunnel slots. Nothing happens to the subscription itself."),
                                           qsTr("Delete"),
                                           qsTr("Cancel"),
                                           function() {
                                               // Peers rows must also be revoked on the api: only
                                               // removed rows would keep counting against the
                                               // per-device tunnel limit and strand the address.
                                               PeersImportController.forgetSubscription(value)
                                               ServersModel.removeSubscription(value)
                                               if (root.subscriptionFilter === value) {
                                                   root.subscriptionFilter = ""
                                                   SettingsController.serversSubscriptionFilter = "all"
                                               }
                                               root.rebuildSubscriptionsModel()
                                               root.rebuildGroups()
                                           },
                                           function() {})
                    }
                }
            }

            // The per-list ping button is gone: measuring lives in the page
            // header next to the subscription reload, so there is only one
            // control for it.
            VerticalRadioButton {
                id: autoSelectButton
                objectName: "autoSelectButton"

                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16

                text: qsTr("Auto-select")
                descriptionText: qsTr("Fastest available server")
                leadingImageSource: "qrc:/countriesFlags/images/flagKit/PIRATE.svg"
                leadingImageSize: 32

                checked: SettingsController.autoServerSelection
                checkable: true

                ButtonGroup.group: serversRadioButtonGroup

                onClicked: {
                    SettingsController.autoServerSelection = true
                    if (ConnectionController.isConnected) {
                        ConnectionController.openConnection()
                    }
                }

                Keys.onEnterPressed: autoSelectButton.clicked()
                Keys.onReturnPressed: autoSelectButton.clicked()
            }
        }
    }

    model: groupsModel

    Connections {
        target: ApiConfigsController
        function onReloadSubscriptionConfigsFinished(success) {
            if (!success) return
            // After a server-list update the list is rebuilt from the stored
            // subscription. The persisted env filter is forced back to "all":
            // the connect path still reads it and an old value would silently
            // keep limiting auto-select to one env.
            SettingsController.serversEnvFilter = "all"
            const savedSub = SettingsController.serversSubscriptionFilter
            root.subscriptionFilter = savedSub === "" || savedSub === "all" ? "" : savedSub
            root.rebuildSubscriptionsModel()
            root.rebuildGroups()
        }
    }

    Connections {
        target: ServersModel
        function onDefaultServerIndexChanged(serverIndex) {
            root.selectedIndex = serverIndex
            root.rebuildGroups()
        }
        function onAvailableSubscriptionsChanged() {
            root.rebuildSubscriptionsModel()
            root.rebuildGroups()
        }
        function onDataChanged() {
            root.rebuildGroups()
        }
        // Rows can be added or dropped without touching the filters: the peers
        // cards arrive after the subscription is fetched, and a revoked grant
        // removes them mid-session. A model reset (addServers / syncPeerCards)
        // changes nothing the signals above report, so the group list stayed
        // stale and the new card was invisible until the screen was reopened.
        function onModelReset() {
            root.rebuildGroups()
        }
    }

    delegate: Item {
        id: groupDelegate

        property var groupData: model
        implicitWidth: root.width
        implicitHeight: groupContent.implicitHeight

        ColumnLayout {
            id: groupContent

            anchors.fill: parent
            anchors.rightMargin: 16
            anchors.leftMargin: 16
            spacing: 0

            RowLayout {
                // The header is only useful when expanding reveals something the
                // cards do not already show: pool members of the same protocol.
                // A peers pool is one row (its protocol is chosen in the server
                // card) — so its group wrapper stays headless: no title, no
                // count badge, no expander, no indented child.
                visible: !root.groupIsPeerPool(groupData)
                         && root.groupVariants(groupData).length > (groupData.expanded ? 1 : root.visibleCardCount(groupData))
                implicitHeight: 56
                Layout.fillWidth: true

                VerticalRadioButton {
                    Layout.fillWidth: true
                    text: groupData.title
                    descriptionText: root.groupVariants(groupData).length + "×"
                    checkable: false
                    onClicked: root.toggleGroup(groupData.key)
                }

                CaptionTextType {
                    Layout.alignment: Qt.AlignVCenter
                    text: groupData.expanded ? "−" : "+"
                    color: DopamineStyle.color.paleGray
                }
            }

            Repeater {
                model: root.groupVariants(groupData)

                delegate: Item {
                    id: variantDelegate

                    property var variant: modelData

                    // A legacy v1 group collapsed renders one card per protocol: a
                    // later row of a protocol already on screen hides itself
                    // (layouts skip invisible children, so it takes no space). A
                    // peers pool is one row. Doing it with visibility keeps the
                    // Repeater bound to the one array that survives the ListModel
                    // round-trip — building a second, derived array produced rows
                    // with empty titles.
                    visible: root.cardIsVisible(groupData, index)

                    implicitWidth: groupContent.width
                    implicitHeight: variantContent.implicitHeight

                    ColumnLayout {
                        id: variantContent

                        anchors.fill: parent
                        spacing: 0

                        RowLayout {
                            Layout.fillWidth: true

                            VerticalRadioButton {
                                id: serverRadioButton
                                objectName: "serverRadioButton"
                                // The flag sits after the check circle, not before
                                // it: row order is checkbox → flag → name.
                                leadingImageSource: root.isElephant(variant)
                                                    ? "qrc:/countriesFlags/images/flagKit/ELEPHANT.svg"
                                                    : (variant.countryCode !== ""
                                                       ? "qrc:/countriesFlags/images/flagKit/" + root.flagCountryCode(variant.countryCode) + ".svg"
                                                       : "")
                                leadingImageSize: root.isElephant(variant) ? 32 : 24

                                Layout.fillWidth: true

                                text: variant.rowTitle !== undefined ? variant.rowTitle : variant.name
                                // One line, always: with ping digits on the right
                                // a wrapped "Czechia | Selfhosted" pushed the row
                                // to two lines. The name keeps its full size and
                                // elides instead of shrinking to unreadable text.
                                textMaximumLineCount: 1
                                textAutoFit: false
                                // The subtitle names the dialed protocol only
                                // while the row has no protocol pills: with
                                // pills the label is already on screen, so the
                                // subtitle keeps the description it had before
                                // the protocol was appended there. The raw tag
                                // decides it, so AmneziaWG and AmneziaWG Mobile
                                // rows stay distinguishable.
                                descriptionText: peerProtocolPills.visible
                                                 ? (variant.isServerFromGatewayApi ? "" : variant.serverDescription)
                                                 : root.protocolLabel(root.protocolKeyOf(variant))

                                checked: variant.sourceIndex === root.selectedIndex && !SettingsController.autoServerSelection
                                // Dot colour: green on the card the tunnel actually
                                // runs on, white on the row the user selected while
                                // disconnected, asset colour otherwise.
                                innerCircleColor: {
                                    // Touch the state text so the dot repaints when
                                    // the tunnel moves (invokables alone don't notify).
                                    const state = ConnectionController.connectionStateText
                                    const selected = variant.sourceIndex === root.selectedIndex
                                                     && !SettingsController.autoServerSelection
                                    if (ConnectionController.isConnected) {
                                        // The row the user dialled is the one carrying
                                        // the tunnel; identity checks below only cover
                                        // the case where rows were rebuilt underneath.
                                        if (selected) {
                                            return "#34D399"
                                        }
                                        if (variant.peerCardId
                                                && ConnectionController.isConnectedToCard(variant.peerCardId)) {
                                            return "#34D399"
                                        }
                                        if (variant.sourceIndex !== undefined
                                                && ConnectionController.isConnectedToRow(variant.sourceIndex)) {
                                            return "#34D399"
                                        }
                                    }
                                    return selected ? DopamineStyle.color.paleGray : ""
                                }
                                checkable: true

                                ButtonGroup.group: serversRadioButtonGroup

                                onClicked: {
                                    root.tapServerRow(variant.sourceIndex)
                                }

                                Keys.onEnterPressed: serverRadioButton.clicked()
                                Keys.onReturnPressed: serverRadioButton.clicked()
                            }

                            RowLayout {
                                Layout.alignment: Qt.AlignVCenter
                                Layout.rightMargin: 8
                                spacing: 6

                                visible: variant.healthLatency !== -2

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: 10
                                    height: 10
                                    radius: 5

                                    visible: variant.healthLatency >= 0
                                    // A proxy-protocol row carries a ping, not a
                                    // tunnel handshake: a neutral dot, no verdict
                                    // colour that would read as "tunnel is up".
                                    color: variant.healthPing
                                           ? DopamineStyle.color.paleGray
                                           : (variant.healthLatency < 120 ? "#34C759" : (variant.healthLatency < 300 ? "#FF9F0A" : "#FF453A"))
                                }

                                Image {
                                    Layout.alignment: Qt.AlignVCenter
                                    sourceSize.width: 12
                                    sourceSize.height: 12

                                    // the cross means "no handshake" — meaningless for a ping
                                    visible: !variant.healthPing && variant.healthLatency === -1
                                    source: "qrc:/images/controls/close.svg"

                                    layer.enabled: true
                                    layer.effect: ColorOverlay {
                                        color: "#FF453A"
                                    }
                                }

                                CaptionTextType {
                                    Layout.alignment: Qt.AlignVCenter

                                    visible: SettingsController.isServerPingTextVisible
                                    text: variant.healthLatency >= 0
                                          ? (variant.healthPing ? "ping " + variant.healthLatency + " ms"
                                                                : variant.healthLatency + " ms")
                                          : qsTr("offline")
                                    color: variant.healthPing
                                           ? DopamineStyle.color.paleGray
                                           : (variant.healthLatency >= 0
                                              ? (variant.healthLatency < 120 ? "#34C759" : (variant.healthLatency < 300 ? "#FF9F0A" : "#FF453A"))
                                              : DopamineStyle.color.vibrantRed)
                                }
                            }

                            ImageButtonType {
                                id: serverInfoButton
                                objectName: "serverInfoButton"

                                image: "qrc:/images/controls/settings.svg"
                                imageColor: DopamineStyle.color.paleGray

                                implicitWidth: 56
                                implicitHeight: 56

                                z: 1

                                onClicked: function() {
                                    ServersModel.processedIndex = variant.sourceIndex

                                    if (ServersModel.getProcessedServerData("isServerFromGatewayApi")) {
                                        PageController.busyOn()
                                        let result = ApiSettingsController.getAccountInfo(false)
                                        PageController.busyOff()
                                        if (!result) return
                                        PageController.goToPage(PageEnum.PageSettingsApiServerInfo)
                                    } else {
                                        PageController.goToPage(PageEnum.PageSettingsServerInfo)
                                    }
                                }
                            }
                        }

                        // A peers pool serves one card per protocol, so its row
                        // carries the protocol pills right under the pool name:
                        // a tap re-points the row at that protocol's card, the
                        // subtitle then names it and connect dials that card.
                        // Only while the pool serves more than one protocol — a
                        // single-protocol row has nothing to switch, and the
                        // dropdown selectors stay out of the list for good.
                        // Indent matches the row title inside VerticalRadioButton
                        // (8 + its 56 px indicator) past the flag.
                        RowLayout {
                            id: peerProtocolPills

                            Layout.fillWidth: true
                            Layout.leftMargin: 64 + (root.isElephant(variant) ? 32 : 24) + 8
                            Layout.rightMargin: 8
                            Layout.bottomMargin: 4

                            spacing: 8
                            // Pills sit tighter under the server name: the gap
                            // between the country line and the protocol row read
                            // as a separate block.
                            Layout.topMargin: -7

                            // Visible even with a single protocol: the row then
                            // shows that protocol as the selected pill, so every
                            // pool looks the same. Only multi-protocol rows are
                            // tappable — a single one is a label, not a chooser.
                            visible: variant.isPeerPool === true
                                     && variant.peerVariants !== undefined
                                     && variant.peerVariants.length > 0

                            Repeater {
                                model: peerProtocolPills.visible ? variant.peerVariants : []

                                delegate: Rectangle {
                                    id: peerProtocolPill

                                    property var variantEntry: modelData

                                    readonly property bool active: variantEntry !== undefined
                                                                   && (variant.peerCardId || "") === variantEntry.cardId

                                    implicitWidth: peerProtocolPillLabel.implicitWidth + 24
                                    implicitHeight: 28

                                    radius: 8

                                    // Both states read the same: theme ink on a
                                    // transparent plate, one border width — only
                                    // the border colour tells the selection.
                                    color: "transparent"
                                    border.width: 1
                                    border.color: peerProtocolPill.active ? DopamineStyle.color.goldenApricot
                                                                          : DopamineStyle.color.slateGray

                                    CaptionTextType {
                                        id: peerProtocolPillLabel

                                        anchors.centerIn: parent

                                        text: root.protocolLabel(peerProtocolPill.variantEntry
                                                                 ? peerProtocolPill.variantEntry.protocol : "")
                                        color: DopamineStyle.color.paleGray
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        // A single-protocol row has nothing to
                                        // switch: the pill is a label, so it must
                                        // not react to taps.
                                        enabled: variant.peerVariants !== undefined
                                                 && variant.peerVariants.length > 1
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor

                                        onClicked: {
                                            const entry = peerProtocolPill.variantEntry
                                            if (entry && (entry.cardId || "") !== ""
                                                    && entry.cardId !== variant.peerCardId) {
                                                // Root scope: switches the protocol,
                                                // re-measures it and re-dials when this
                                                // row is the connected one.
                                                root.protocolPillSelected(variant.sourceIndex, entry.cardId)
                                            }
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        DividerType {
                            Layout.fillWidth: true
                            Layout.leftMargin: 0
                            Layout.rightMargin: 0
                        }
                    }
                }
            }
        }
    }
}
