package com.bitchat.android.features.voice

import android.content.Context
import com.bitchat.android.model.BitchatMessage

// MVP stub (FRKN mesh embed): live voice is not shipped. Incoming voice
// frames are consumed (handleFrame = true) so they never surface as messages.
enum class LiveVoiceScope { DIRECT_MESSAGE, PUBLIC_MESH }

class LiveVoiceManager private constructor(private val context: Context) {

    companion object {
        @Volatile
        private var instance: LiveVoiceManager? = null

        @JvmStatic
        fun getInstance(context: Context): LiveVoiceManager =
            instance ?: synchronized(this) {
                instance ?: LiveVoiceManager(context.applicationContext).also { instance = it }
            }
    }

    fun handleFrame(
        peerID: String,
        nickname: String,
        scope: LiveVoiceScope,
        payload: ByteArray,
        timestampMs: Long
    ): Boolean = true

    fun absorbFinalizedVoiceNote(message: BitchatMessage): Boolean = false
}
