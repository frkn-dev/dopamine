import Foundation

// MVP stub (FRKN mesh embed): the upstream NostrIdentity is backed by
// secp256k1 (P256K SPM package) and exists for Nostr features we don't ship.
// Only the shape BLEService/BoardStore touch is kept.
struct NostrIdentity: Codable {
    let privateKey: Data
    let publicKey: Data
    let npub: String // Bech32-encoded public key

    var publicKeyHex: String { publicKey.map { String(format: "%02x", $0) }.joined() }
}
