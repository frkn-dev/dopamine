import Foundation

// MVP stub (FRKN mesh embed): a Nostr event is only a JSON carrier for us —
// BLE relay code passes these bytes through untouched; no signature handling.
struct NostrEvent {
    private let dict: [String: Any]

    init(from dict: [String: Any]) throws {
        self.dict = dict
    }

    func jsonString() throws -> String {
        let data = try JSONSerialization.data(withJSONObject: dict)
        return String(decoding: data, as: UTF8.self)
    }
}
