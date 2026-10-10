package com.bitchat.android.services

// MVP stub (FRKN mesh embed): upstream maps peer IDs to canonical contact
// conversation IDs via its contact directory. We keep the peer ID as-is.
object ContactDirectory {
    fun canonicalConversationId(peerID: String): String = peerID
}
