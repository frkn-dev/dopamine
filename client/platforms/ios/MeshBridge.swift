import Foundation
import CoreBluetooth

// Thin facade over the vendored BitChat BLE mesh core (client/3rd/bitchat-ios).
// Exposed to C++ via Swift C++ interop (see common/logger for the same pattern):
// top-level public functions land in the generated Dopamine-Swift.h as
// Dopamine::meshStart() etc.
//
// Interop note: the core advertises the RELEASE service UUID, so a Release build
// of Dopamine meshes with the stock BitChat app; Debug builds use the testnet
// UUID and only see other debug clients.

final class MeshBridge: NSObject {
    static let shared = MeshBridge()

    var messageCallback: MeshMessageCallback?
    var peerCountCallback: MeshPeerCountCallback?
    var privateMessageCallback: MeshMessageCallback?

    private var bleService: BLEService?
    private var nickname = "anon"
    private(set) var isRunning = false

    func setNickname(_ nickname: String) {
        let trimmed = nickname.trimmingCharacters(in: .whitespacesAndNewlines)
        self.nickname = trimmed.isEmpty ? "anon" : trimmed
        bleService?.setNickname(self.nickname)
    }

    func start() {
        guard bleService == nil else { return }

        let keychain = KeychainManager.makeDefault()
        let service = BLEService(
            keychain: keychain,
            idBridge: NostrIdentityBridge(),
            identityManager: SecureIdentityStateManager(keychain)
        )
        service.delegate = self
        service.peerEventsDelegate = self
        bleService = service
        service.startServices()
        service.setNickname(nickname)
        isRunning = true
    }

    func stop() {
        bleService?.stopServices()
        bleService = nil
        isRunning = false
        DispatchQueue.main.async { [weak self] in
            self?.peerCountCallback?(0)
        }
    }

    func send(_ text: String) {
        guard let service = bleService else { return }
        // Transport wrapper hops onto the service queue — safe from the main thread
        service.sendMessage(text, mentions: [])
    }

    func peerCount() -> Int {
        bleService?.currentPeerSnapshots().count ?? 0
    }

    /// ICQ-style stable ID: fingerprint of the device's Noise identity key.
    func myId() -> String {
        bleService?.noiseIdentityFingerprint() ?? ""
    }

    /// Device Noise static public key (hex) — registered with the relay server to get a UIN.
    func myPubkey() -> String {
        bleService?.noiseStaticPublicKeyData().hexEncodedString() ?? ""
    }

    /// Device Ed25519 signing public key (hex) — registered alongside, verifies send/pull signatures.
    func mySigningPubkey() -> String {
        bleService?.noiseSigningPublicKeyData().hexEncodedString() ?? ""
    }

    /// Ed25519 signature (base64) over the UTF-8 bytes of the canonical string.
    /// Used to authenticate relay requests (send/pull) against the registered key.
    func sign(_ canonical: String) -> String? {
        bleService?.noiseSignData(Data(canonical.utf8))?.base64EncodedString()
    }

    /// One-way Noise X seal (no handshake) to a recipient's static pubkey (hex).
    /// Returns base64 envelope for the relay transport, nil on failure.
    func sealTo(_ recipientPubkeyHex: String, _ text: String) -> String? {
        guard let service = bleService, let recipientKey = Data(hexString: recipientPubkeyHex) else { return nil }
        guard let envelope = try? service.noiseSealPayload(Data(text.utf8), recipientStaticKey: recipientKey) else { return nil }
        return envelope.base64EncodedString()
    }

    /// Opens a relay envelope (base64). Returns JSON {"fp": senderFingerprint, "text": plaintext} or "" on failure.
    func unseal(_ envelopeB64: String) -> String {
        guard let service = bleService, let envelope = Data(base64Encoded: envelopeB64) else { return "" }
        guard let opened = try? service.noiseOpenSealedPayload(envelope),
              let text = String(data: opened.payload, encoding: .utf8) else { return "" }
        let result: [String: String] = ["fp": opened.senderFingerprint, "text": text]
        guard let data = try? JSONSerialization.data(withJSONObject: result) else { return "" }
        return String(decoding: data, as: UTF8.self)
    }

    /// Currently reachable peers with identity fingerprints: [{"fp": "...", "nick": "..."}]
    func peersJson() -> String {
        guard let service = bleService else { return "[]" }
        var peers: [[String: String]] = []
        for snapshot in service.currentPeerSnapshots() {
            if let fingerprint = service.getFingerprint(for: snapshot.peerID), !fingerprint.isEmpty {
                peers.append(["fp": fingerprint, "nick": snapshot.nickname])
            }
        }
        guard let data = try? JSONSerialization.data(withJSONObject: peers) else { return "[]" }
        return String(decoding: data, as: UTF8.self)
    }

    /// 1:1 to a reachable peer by identity fingerprint. The core queues the
    /// message and runs the Noise handshake automatically. false = peer not
    /// in mesh range right now.
    func sendToFingerprint(_ fingerprint: String, _ text: String) -> Bool {
        guard let service = bleService else { return false }
        for snapshot in service.currentPeerSnapshots() where service.getFingerprint(for: snapshot.peerID) == fingerprint {
            service.sendPrivateMessage(text, to: snapshot.peerID, recipientNickname: snapshot.nickname,
                                       messageID: UUID().uuidString)
            return true
        }
        return false
    }
}

extension MeshBridge: BitchatDelegate {
    func didReceiveMessage(_ message: BitchatMessage) {
        // private 1:1 (Noise-encrypted) — report with the sender's identity fingerprint
        guard message.isPrivate, let senderPeerID = message.senderPeerID else { return }
        let fingerprint = bleService?.getFingerprint(for: senderPeerID) ?? ""
        let text = message.content
        DispatchQueue.main.async { [weak self] in
            guard let callback = self?.privateMessageCallback else { return }
            fingerprint.withCString { fpPtr in
                text.withCString { textPtr in
                    callback(fpPtr, textPtr)
                }
            }
        }
    }

    // FRKN mesh embed: in this core private 1:1 actually arrives as a Noise
    // payload (NOISE_ENCRYPTED → PRIVATE_MESSAGE TLV), not didReceiveMessage —
    // without this hook inbound private messages were silently dropped.
    func didReceiveNoisePayload(from peerID: PeerID, type: NoisePayloadType, payload: Data, timestamp: Date) {
        guard type == .privateMessage, let packet = PrivateMessagePacket.decode(from: payload) else { return }
        let fingerprint = bleService?.getFingerprint(for: peerID) ?? ""
        let text = packet.content
        DispatchQueue.main.async { [weak self] in
            guard let callback = self?.privateMessageCallback else { return }
            fingerprint.withCString { fpPtr in
                text.withCString { textPtr in
                    callback(fpPtr, textPtr)
                }
            }
        }
    }

    func didConnectToPeer(_ peerID: PeerID) {}
    func didDisconnectFromPeer(_ peerID: PeerID) {}
    func didUpdatePeerList(_ peers: [PeerID]) {}
    func didUpdateBluetoothState(_ state: CBManagerState) {}

    func didReceivePublicMessage(from peerID: PeerID, nickname: String, content: String,
                                 timestamp: Date, messageID: String?) {
        DispatchQueue.main.async { [weak self] in
            guard let callback = self?.messageCallback else { return }
            nickname.withCString { nickPtr in
                content.withCString { contentPtr in
                    callback(nickPtr, contentPtr)
                }
            }
        }
    }
}

extension MeshBridge: TransportPeerEventsDelegate {
    @MainActor func didUpdatePeerSnapshots(_ snapshots: [TransportPeerSnapshot]) {
        peerCountCallback?(Int32(snapshots.count))
    }
}

// MARK: - C++ interop surface

public typealias MeshMessageCallback = @convention(c) (UnsafePointer<CChar>?, UnsafePointer<CChar>?) -> Void
public typealias MeshPeerCountCallback = @convention(c) (Int32) -> Void

public func meshSetMessageCallback(_ callbackAddress: UnsafeMutableRawPointer?) {
    if let address = callbackAddress {
        MeshBridge.shared.messageCallback = unsafeBitCast(address, to: MeshMessageCallback.self)
    } else {
        MeshBridge.shared.messageCallback = nil
    }
}

public func meshSetPeerCountCallback(_ callbackAddress: UnsafeMutableRawPointer?) {
    if let address = callbackAddress {
        MeshBridge.shared.peerCountCallback = unsafeBitCast(address, to: MeshPeerCountCallback.self)
    } else {
        MeshBridge.shared.peerCountCallback = nil
    }
}

public func meshSetNickname(_ nickname: UnsafePointer<CChar>) {
    MeshBridge.shared.setNickname(String(cString: nickname))
}

public func meshStart() {
    MeshBridge.shared.start()
}

public func meshStop() {
    MeshBridge.shared.stop()
}

public func meshSend(_ text: UnsafePointer<CChar>) {
    MeshBridge.shared.send(String(cString: text))
}

public func meshPeerCount() -> Int32 {
    return Int32(MeshBridge.shared.peerCount())
}

public func meshSetPrivateMessageCallback(_ callbackAddress: UnsafeMutableRawPointer?) {
    if let address = callbackAddress {
        MeshBridge.shared.privateMessageCallback = unsafeBitCast(address, to: MeshMessageCallback.self)
    } else {
        MeshBridge.shared.privateMessageCallback = nil
    }
}

public func meshMyId() -> std.string {
    return std.string(MeshBridge.shared.myId())
}

public func meshMyPubkey() -> std.string {
    return std.string(MeshBridge.shared.myPubkey())
}

public func meshMySigningPubkey() -> std.string {
    return std.string(MeshBridge.shared.mySigningPubkey())
}

public func meshSign(_ canonical: UnsafePointer<CChar>) -> std.string {
    return std.string(MeshBridge.shared.sign(String(cString: canonical)) ?? "")
}

public func meshSealTo(_ recipientPubkeyHex: UnsafePointer<CChar>, _ text: UnsafePointer<CChar>) -> std.string {
    return std.string(MeshBridge.shared.sealTo(String(cString: recipientPubkeyHex), String(cString: text)) ?? "")
}

public func meshUnseal(_ envelopeB64: UnsafePointer<CChar>) -> std.string {
    return std.string(MeshBridge.shared.unseal(String(cString: envelopeB64)))
}

public func meshPeersJson() -> std.string {
    return std.string(MeshBridge.shared.peersJson())
}

public func meshSendTo(_ fingerprint: UnsafePointer<CChar>, _ text: UnsafePointer<CChar>) -> Bool {
    return MeshBridge.shared.sendToFingerprint(String(cString: fingerprint), String(cString: text))
}
