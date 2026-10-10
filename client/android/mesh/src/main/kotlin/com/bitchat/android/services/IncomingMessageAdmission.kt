package com.bitchat.android.services

import com.bitchat.android.model.BitchatMessage

// MVP stub (FRKN mesh embed): upstream reflects incoming messages into its
// process-wide UI store before dispatch. We keep no message store, so
// admission only rejects private messages without a sender.
internal object IncomingMessageAdmission {
    fun admitToAppState(message: BitchatMessage): Boolean =
        !message.isPrivate || !message.senderPeerID.isNullOrBlank()
}
