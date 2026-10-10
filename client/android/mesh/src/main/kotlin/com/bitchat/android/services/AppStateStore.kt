package com.bitchat.android.services

import com.bitchat.android.model.DeliveryStatus
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

// MVP stub (FRKN mesh embed): upstream keeps the whole UI state in this
// process-wide store. Only the transport peer bookkeeping the BLE core
// actually reads/writes is kept; message status updates are no-ops.
object AppStateStore {
    private val peerIdsByTransport = mutableMapOf<String, Set<String>>()
    private val directPeerIdsByTransport = mutableMapOf<String, Set<String>>()

    private val _peers = MutableStateFlow<List<String>>(emptyList())
    val peers: StateFlow<List<String>> = _peers.asStateFlow()

    private val _directPeers = MutableStateFlow<Set<String>>(emptySet())
    val directPeers: StateFlow<Set<String>> = _directPeers.asStateFlow()

    @Synchronized
    fun setTransportPeers(transportId: String, ids: List<String>) {
        peerIdsByTransport[transportId] = ids.toSet()
        _peers.value = peerIdsByTransport.values.flatten().distinct()
    }

    @Synchronized
    fun clearTransportPeers(transportId: String) {
        peerIdsByTransport.remove(transportId)
        _peers.value = peerIdsByTransport.values.flatten().distinct()
    }

    @Synchronized
    fun setTransportDirectPeers(transportId: String, ids: Collection<String>) {
        directPeerIdsByTransport[transportId] = ids.toSet()
        _directPeers.value = directPeerIdsByTransport.values.flatten().toSet()
    }

    @Synchronized
    fun clearTransportDirectPeers(transportId: String) {
        directPeerIdsByTransport.remove(transportId)
        _directPeers.value = directPeerIdsByTransport.values.flatten().toSet()
    }

    fun getDirectPeers(): Set<String> = _directPeers.value

    fun updatePrivateMessageStatus(messageID: String, status: DeliveryStatus) {}

    fun markPrivateMessageRead(messageID: String) {}

    fun canonicalizePrivateChats() {}
}
