import Foundation

struct XrayConfig: Decodable {
    let dns1: String?
    let dns2: String?
    let splitTunnelType: Int?
    let splitTunnelSites: [String]?
    let splitTunnelIncludeSites: [String]?
    let splitTunnelExcludeSites: [String]?
    let config: String
}
