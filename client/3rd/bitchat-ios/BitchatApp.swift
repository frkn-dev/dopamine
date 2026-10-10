import Foundation

// MVP shim (FRKN mesh embed): upstream BitchatApp.swift is the SwiftUI app
// entry point; only the bundleID constant is referenced by KeychainManager.
enum BitchatApp {
    static let bundleID = "org.frkn.dopamine"
}
