package com.bitchat.android.services

import com.bitchat.android.model.ReadReceipt

// MVP stub (FRKN mesh embed): upstream routes messages between the mesh and
// Nostr transports. We ship no Nostr transport, so there is never a router
// instance; geohash read receipts are dropped (GeohashAliasRegistry is empty).
class MessageRouter private constructor() {

    companion object {
        @JvmStatic
        fun tryGetInstance(): MessageRouter? = null
    }

    fun sendReadReceipt(receipt: ReadReceipt, toPeerID: String) {}

    fun onSessionEstablished(peerID: String) {}
}
