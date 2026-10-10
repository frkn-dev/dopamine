package com.bitchat.android.ui.debug

import com.bitchat.android.protocol.BitchatPacket
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.Date

// MVP stub (FRKN mesh embed): upstream feeds a Compose debug sheet from this
// manager. We ship no debug UI — the toggles stay at upstream defaults and
// logging calls are no-ops.
class DebugSettingsManager private constructor() {

    companion object {
        private val instance = DebugSettingsManager()

        @JvmStatic
        fun getInstance(): DebugSettingsManager = instance
    }

    private val _gattServerEnabled = MutableStateFlow(true)
    val gattServerEnabled: StateFlow<Boolean> = _gattServerEnabled.asStateFlow()

    private val _gattClientEnabled = MutableStateFlow(true)
    val gattClientEnabled: StateFlow<Boolean> = _gattClientEnabled.asStateFlow()

    private val _packetRelayEnabled = MutableStateFlow(true)
    val packetRelayEnabled: StateFlow<Boolean> = _packetRelayEnabled.asStateFlow()

    private val _bleEnabled = MutableStateFlow(true)
    val bleEnabled: StateFlow<Boolean> = _bleEnabled.asStateFlow()

    private val _maxConnectionsOverall = MutableStateFlow(8)
    val maxConnectionsOverall: StateFlow<Int> = _maxConnectionsOverall.asStateFlow()

    private val _maxServerConnections = MutableStateFlow(8)
    val maxServerConnections: StateFlow<Int> = _maxServerConnections.asStateFlow()

    private val _maxClientConnections = MutableStateFlow(8)
    val maxClientConnections: StateFlow<Int> = _maxClientConnections.asStateFlow()

    fun addScanResult(scanResult: DebugScanResult) {}

    fun logIncoming(packet: BitchatPacket, fromPeerID: String, fromNickname: String?, fromDeviceAddress: String?, myPeerID: String) {}

    fun logIncomingPacket(senderPeerID: String, senderNickname: String?, messageType: String, viaDeviceId: String?) {}

    fun logOutgoing(packetType: String, toPeerID: String?, toNickname: String?, toDeviceAddress: String?,
                    previousHopPeerID: String? = null, packetVersion: UByte = 1u, routeInfo: String? = null) {}

    fun logPacketRelayDetailed(packetType: String, senderPeerID: String?, senderNickname: String?,
                               fromPeerID: String?, fromNickname: String?, fromDeviceAddress: String?,
                               toPeerID: String?, toNickname: String?, toDeviceAddress: String?,
                               ttl: UByte?, isRelay: Boolean = true, packetVersion: UByte = 1u,
                               routeInfo: String? = null) {}

    fun setNicknameResolver(resolver: (String) -> String?) {}

    fun logPeerConnection(peerID: String, nickname: String, deviceAddress: String, isInbound: Boolean) {}
}

data class DebugScanResult(
    val deviceName: String?,
    val deviceAddress: String,
    val rssi: Int,
    val peerID: String?,
    val timestamp: Date = Date()
)
