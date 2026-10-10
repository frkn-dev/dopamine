package com.bitchat.android.ui

import android.content.Context
import androidx.core.app.NotificationManagerCompat

// MVP stub (FRKN mesh embed): upstream posts system notifications for
// background DMs. Dopamine surfaces mesh messages in its own QML UI, so
// notifications are dropped.
class NotificationManager(
    private val context: Context,
    private val notificationManager: NotificationManagerCompat
) {
    fun setAppBackgroundState(inBackground: Boolean) {}

    fun showPrivateMessageNotification(senderPeerID: String, senderNickname: String, messageContent: String) {}
}
