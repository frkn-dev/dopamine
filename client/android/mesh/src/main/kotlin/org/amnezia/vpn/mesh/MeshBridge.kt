package org.amnezia.vpn.mesh

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Base64
import android.util.Log
import androidx.core.app.ActivityCompat
import com.bitchat.android.mesh.BluetoothMeshDelegate
import com.bitchat.android.mesh.BluetoothMeshService
import com.bitchat.android.model.BitchatMessage
import com.bitchat.android.noise.NoiseEncryptionService
import com.bitchat.android.service.MeshServiceHolder
import com.bitchat.android.util.dataFromHexString
import com.bitchat.android.util.hexEncodedString
import java.util.UUID

/**
 * Thin facade over the vendored BitChat BLE mesh core, Android counterpart of
 * platforms/ios/MeshBridge.swift — same method semantics, exposed to C++ via
 * JNI (see platforms/android/meshBridgeWrapper.cpp). Called from Qt threads;
 * everything core-facing is marshalled onto the main thread.
 *
 * Interop note: the core advertises the RELEASE service UUID
 * (F47B5E2D-...-4B5C), so Dopamine meshes with the stock BitChat app and with
 * release iOS builds.
 */
object MeshBridge {

    private const val TAG = "MeshBridge"
    private const val REQUEST_BLUETOOTH_PERMISSIONS = 77

    private val mainHandler = Handler(Looper.getMainLooper())

    private var appContext: Context? = null
    private var bleService: BluetoothMeshService? = null
    private var noiseService: NoiseEncryptionService? = null
    @Volatile
    private var isRunning = false
    @Volatile
    private var startRequested = false

    @JvmStatic
    fun start(context: Context) {
        appContext = context.applicationContext
        startRequested = true
        mainHandler.post {
            if (isRunning) return@post
            if (missingBluetoothPermissions(context).isNotEmpty()) {
                // request first, then start the core once the user grants —
                // the BLE stack refuses to start without permissions
                requestBluetoothPermissions(context)
                startWhenPermissionsGranted(context, 0)
                return@post
            }
            startCore(context)
        }
    }

    private fun startCore(context: Context) {
        if (isRunning) return
        val service = MeshServiceHolder.getOrCreate(context.applicationContext)
        service.delegate = meshDelegate
        bleService = service
        service.startServices()
        isRunning = true
    }

    private fun startWhenPermissionsGranted(context: Context, attempt: Int) {
        if (attempt >= 90) return // ~90s of waiting; the next start() call retries
        mainHandler.postDelayed({
            if (!startRequested || isRunning) return@postDelayed
            if (missingBluetoothPermissions(context).isEmpty()) {
                startCore(context)
            } else {
                startWhenPermissionsGranted(context, attempt + 1)
            }
        }, 1000)
    }

    @JvmStatic
    fun setNickname(nickname: String) {
        com.bitchat.android.services.NicknameProvider.setNickname(nickname)
        mainHandler.post {
            if (!isRunning) return@post
            try { bleService?.sendBroadcastAnnounce() } catch (_: Exception) { }
        }
    }

    @JvmStatic
    fun stop() {
        startRequested = false
        mainHandler.post {
            if (!isRunning) return@post
            try { bleService?.stopServices() } catch (_: Exception) { }
            bleService = null
            isRunning = false
            onPeerCount(0)
        }
    }

    @JvmStatic
    fun send(text: String) {
        mainHandler.post {
            try { bleService?.sendMessage(text, emptyList()) } catch (_: Exception) { }
        }
    }

    /** 1:1 to a reachable peer by identity fingerprint. false = peer not in mesh range right now. */
    @JvmStatic
    fun sendTo(fingerprint: String, text: String): Boolean {
        val service = bleService ?: run {
            Log.w(TAG, "sendTo: no service")
            return false
        }
        if (!isRunning) {
            Log.w(TAG, "sendTo: not running")
            return false
        }
        return try {
            val nicknames = service.getPeerNicknames()
            Log.w(TAG, "sendTo: looking for fp ${fingerprint.take(12)}… among ${nicknames.size} peers")
            for ((peerID, nickname) in nicknames) {
                val fp = service.getPeerFingerprint(peerID)
                Log.w(TAG, "sendTo: peer $peerID fp=${fp?.take(12)}…")
                if (fp == fingerprint) {
                    mainHandler.post {
                        try {
                            service.sendPrivateMessage(text, peerID, nickname, UUID.randomUUID().toString())
                            Log.w(TAG, "sendTo: posted private message to $peerID")
                        } catch (e: Exception) {
                            Log.w(TAG, "sendTo: post failed: $e")
                        }
                    }
                    return true
                }
            }
            Log.w(TAG, "sendTo: fingerprint not found in peers")
            false
        } catch (e: Exception) {
            Log.w(TAG, "sendTo: exception $e")
            false
        }
    }

    @JvmStatic
    fun peerCount(): Int {
        if (!isRunning) return 0
        return try { bleService?.getPeerNicknames()?.size ?: 0 } catch (_: Exception) { 0 }
    }

    /** Currently reachable peers with identity fingerprints: [{"fp": "...", "nick": "..."}] */
    @JvmStatic
    fun peersJson(): String {
        val service = bleService ?: return "[]"
        if (!isRunning) return "[]"
        return try {
            val peers = StringBuilder("[")
            var first = true
            for ((peerID, nickname) in service.getPeerNicknames()) {
                val fingerprint = service.getPeerFingerprint(peerID)
                if (fingerprint.isNullOrEmpty()) continue
                if (!first) peers.append(',')
                first = false
                peers.append("{\"fp\":\"").append(fingerprint)
                    .append("\",\"nick\":\"").append(jsonEscape(nickname)).append("\"}")
            }
            peers.append(']').toString()
        } catch (_: Exception) {
            "[]"
        }
    }

    /** ICQ-style stable ID: fingerprint of the device's Noise identity key. */
    @JvmStatic
    fun myFingerprint(): String = getNoiseService()?.getIdentityFingerprint() ?: ""

    /** Device Noise static public key (hex) — registered with the relay server to get a UIN. */
    @JvmStatic
    fun myPubkey(): String = getNoiseService()?.getStaticPublicKeyData()?.hexEncodedString() ?: ""

    /** Device Ed25519 signing public key (hex) — registered alongside, verifies send/pull signatures. */
    @JvmStatic
    fun mySigningPubkey(): String = getNoiseService()?.getSigningPublicKeyData()?.hexEncodedString() ?: ""

    /** Ed25519 signature (base64) over the UTF-8 bytes of the canonical string. */
    @JvmStatic
    fun sign(canonical: String): String {
        val signature = getNoiseService()?.signData(canonical.toByteArray(Charsets.UTF_8)) ?: return ""
        return Base64.encodeToString(signature, Base64.NO_WRAP)
    }

    /** One-way Noise X seal (no handshake) to a recipient's static pubkey (hex). Base64 envelope for the relay. */
    @JvmStatic
    fun sealTo(recipientPubkeyHex: String, text: String): String {
        val noise = getNoiseService() ?: return ""
        val recipientKey = recipientPubkeyHex.dataFromHexString() ?: return ""
        val envelope = noise.sealCourierPayload(text.toByteArray(Charsets.UTF_8), recipientKey) ?: return ""
        return Base64.encodeToString(envelope, Base64.NO_WRAP)
    }

    /** Opens a relay envelope (base64). Returns JSON {"fp": senderFingerprint, "text": plaintext} or "" on failure. */
    @JvmStatic
    fun unseal(envelopeB64: String): String {
        val noise = getNoiseService() ?: return ""
        val envelope = try { Base64.decode(envelopeB64, Base64.DEFAULT) } catch (_: Exception) { return "" }
        val opened = noise.openCourierPayload(envelope) ?: return ""
        val text = String(opened.first, Charsets.UTF_8)
        // fingerprint of the sender's static key, same derivation as the core's calculateFingerprint
        val digest = java.security.MessageDigest.getInstance("SHA-256").digest(opened.second)
        val fp = digest.hexEncodedString()
        return "{\"fp\":\"$fp\",\"text\":\"${jsonEscape(text)}\"}"
    }

    private fun getNoiseService(): NoiseEncryptionService? {
        noiseService?.let { return it }
        val context = appContext ?: return null
        return try {
            NoiseEncryptionService(context).also { noiseService = it }
        } catch (_: Exception) {
            null
        }
    }

    private fun missingBluetoothPermissions(context: Context): List<String> {
        val required = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            listOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_ADVERTISE,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            listOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }
        return required.filter {
            ActivityCompat.checkSelfPermission(context, it) != PackageManager.PERMISSION_GRANTED
        }
    }

    private fun requestBluetoothPermissions(context: Context) {
        val missing = missingBluetoothPermissions(context)
        if (missing.isNotEmpty() && context is Activity) {
            ActivityCompat.requestPermissions(context, missing.toTypedArray(), REQUEST_BLUETOOTH_PERMISSIONS)
        }
    }

    private fun jsonEscape(value: String): String = buildString(value.length) {
        for (c in value) {
            when (c) {
                '\\' -> append("\\\\")
                '"' -> append("\\\"")
                '\n' -> append("\\n")
                '\r' -> append("\\r")
                '\t' -> append("\\t")
                else -> if (c < ' ') append(String.format("\\u%04x", c.code)) else append(c)
            }
        }
    }

    private val meshDelegate = object : BluetoothMeshDelegate {
        override fun didReceiveMessage(message: BitchatMessage) {
            if (message.isPrivate) {
                // private 1:1 (Noise-encrypted) — report with the sender's identity fingerprint
                val senderPeerID = message.senderPeerID ?: return
                val fingerprint = try {
                    bleService?.getPeerFingerprint(senderPeerID)
                } catch (_: Exception) { null } ?: return
                val text = message.content
                mainHandler.post { onPrivateMessage(fingerprint, text) }
            } else {
                val nickname = message.sender
                val text = message.content
                mainHandler.post { onMessage(nickname, text) }
            }
        }

        override fun didUpdatePeerList(peers: List<String>) {
            mainHandler.post { onPeerCount(peers.size) }
        }

        override fun didReceiveChannelLeave(channel: String, fromPeer: String) {}
        override fun didReceiveDeliveryAck(messageID: String, recipientPeerID: String) {}
        override fun didReceiveReadReceipt(messageID: String, recipientPeerID: String) {}
        override fun didReceiveVerifyChallenge(peerID: String, payload: ByteArray, timestampMs: Long) {}
        override fun didReceiveVerifyResponse(peerID: String, payload: ByteArray, timestampMs: Long) {}
        override fun decryptChannelMessage(encryptedContent: ByteArray, channel: String): String? = null
        override fun getNickname(): String? {
            val context = appContext ?: return null
            return com.bitchat.android.services.NicknameProvider.getNickname(context, "")
        }
        override fun isFavorite(peerID: String): Boolean = false
    }

    // JNI functions of the MeshBridgeWrapper class from meshBridgeWrapper.cpp,
    // called by mesh core events (registered via registerNativeMethods)
    @JvmStatic
    external fun onMessage(nickname: String, text: String)

    @JvmStatic
    external fun onPrivateMessage(senderFingerprint: String, text: String)

    @JvmStatic
    external fun onPeerCount(count: Int)
}
