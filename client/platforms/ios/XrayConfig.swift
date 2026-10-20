import Foundation

struct XrayConfig: Decodable {
    let dns1: String?
    let dns2: String?
    let splitTunnelType: Int?
    let splitTunnelSites: [String]?
    let splitTunnelIncludeSites: [String]?
    let splitTunnelExcludeSites: [String]?
    /// Subnets that always bypass the tunnel (Settings -> Local subnets).
    let localSubnetExclusions: [String]?
    let config: String
}
