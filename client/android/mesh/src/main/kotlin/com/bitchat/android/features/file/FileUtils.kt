package com.bitchat.android.features.file

import android.content.Context
import com.bitchat.android.model.BitchatFilePacket
import com.bitchat.android.model.BitchatMessageType

// MVP stub (FRKN mesh embed): file/media transfer is not shipped. Incoming
// file packets still decode and surface as messages with an empty content
// path instead of a saved file.
object FileUtils {

    fun saveIncomingFile(context: Context, file: BitchatFilePacket): String = ""

    fun messageTypeForMime(mime: String): BitchatMessageType {
        val lower = mime.lowercase()
        return when {
            lower.startsWith("image/") -> BitchatMessageType.Image
            lower.startsWith("audio/") -> BitchatMessageType.Audio
            else -> BitchatMessageType.File
        }
    }
}
