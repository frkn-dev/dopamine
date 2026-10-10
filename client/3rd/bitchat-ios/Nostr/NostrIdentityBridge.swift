import Foundation

// MVP stub (FRKN mesh embed): upstream uses this bridge for Nostr favorites
// notifications and geohash identity derivation. We ship neither — returning
// nil keeps public mesh messages working (they just don't carry a nostr npub).
final class NostrIdentityBridge {
    init(keychain: KeychainManagerProtocol? = nil) {}

    func getCurrentNostrIdentity() throws -> NostrIdentity? {
        return nil
    }
}
