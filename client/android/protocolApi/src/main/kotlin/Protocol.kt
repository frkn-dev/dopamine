package org.amnezia.vpn.protocol

import android.content.Context
import android.net.IpPrefix
import android.net.VpnService
import android.net.VpnService.Builder
import android.os.Build
import android.system.OsConstants
import androidx.annotation.RequiresApi
import kotlinx.coroutines.flow.MutableStateFlow
import org.amnezia.vpn.util.Log
import org.amnezia.vpn.util.net.InetNetwork
import org.amnezia.vpn.util.net.IpRange
import org.amnezia.vpn.util.net.IpRangeSet
import org.json.JSONObject

private const val TAG = "Protocol"

const val VPN_SESSION_NAME = "FRKN"

private const val SPLIT_TUNNEL_DISABLE = 0
private const val SPLIT_TUNNEL_INCLUDE = 1
private const val SPLIT_TUNNEL_EXCLUDE = 2

/** JSON key of the local subnet exclusions, see `configLocalSubnetExclusions`. */
private const val LOCAL_SUBNET_EXCLUSIONS_KEY = "localSubnetExclusions"

abstract class Protocol {

    abstract val statistics: Statistics
    protected lateinit var context: Context
    protected lateinit var state: MutableStateFlow<ProtocolState>
    protected lateinit var onError: (String) -> Unit
    protected var isInitialized: Boolean = false

    fun initialize(context: Context, state: MutableStateFlow<ProtocolState>, onError: (String) -> Unit) {
        this.context = context
        this.state = state
        this.onError = onError
        internalInit()
        isInitialized = true
    }

    protected abstract fun internalInit()

    abstract suspend fun startVpn(config: JSONObject, vpnBuilder: Builder, protect: (Int) -> Boolean)

    abstract fun stopVpn()

    abstract fun reconnectVpn(vpnBuilder: Builder, protect: (Int) -> Boolean)

    protected fun ProtocolConfig.Builder.configSplitTunneling(config: JSONObject) {
        if (!allowSplitTunneling) {
            Log.i(TAG, "Global address split tunneling is prohibited, " +
                "only tunneling from the protocol config is used")
            return
        }

        val splitTunnelType = config.optInt("splitTunnelType")
        if (splitTunnelType == SPLIT_TUNNEL_DISABLE) return

        // 2x2 split tunneling: the C++ side flattens the manual site list and
        // the service presets into splitTunnelIncludeSites / splitTunnelExcludeSites;
        // splitTunnelSites is the legacy manual-only list, kept as fallback
        val (addressHandlerFunc, sitesKey) = when (splitTunnelType) {
            SPLIT_TUNNEL_INCLUDE -> ::includeAddress to "splitTunnelIncludeSites"
            SPLIT_TUNNEL_EXCLUDE -> ::excludeAddress to "splitTunnelExcludeSites"

            else -> throw BadConfigException("Unexpected value of the 'splitTunnelType' parameter: $splitTunnelType")
        }

        val splitTunnelSites = config.optJSONArray(sitesKey) ?: config.getJSONArray("splitTunnelSites")
        for (i in 0 until splitTunnelSites.length()) {
            val address = InetNetwork.parse(splitTunnelSites.getString(i))
            addressHandlerFunc(address)
        }
    }

    /**
     * Local subnet exclusions (Settings -> Local subnets): subnets that keep
     * going out of the physical interface instead of the tunnel.
     *
     * Unlike [configSplitTunneling] this is applied on every connection: it is
     * not a split-tunneling direction, so it works with split tunneling off and
     * wins over a preset that sends the same subnet "via VPN".
     */
    protected fun ProtocolConfig.Builder.configLocalSubnetExclusions(config: JSONObject) {
        val exclusions = mutableSetOf<InetNetwork>()
        val rawExclusions = config.optJSONArray(LOCAL_SUBNET_EXCLUSIONS_KEY) ?: return
        for (i in 0 until rawExclusions.length()) {
            val entry = rawExclusions.optString(i).trim()
            if (entry.isEmpty()) continue
            runCatching { InetNetwork.parse(entry) }.getOrNull()?.let { exclusions += it }
        }
        if (exclusions.isEmpty()) return

        if (includedAddresses.isNotEmpty()) {
            // Inclusive split tunneling cannot coexist with an exclude list
            // (see processSplitTunneling()), so the exclusions are cut out of
            // the include list instead: they never enter the tunnel.
            val includes = includedAddresses.toSet()
            includedAddresses.clear()
            includedAddresses += subtract(exclusions, includes)
            return
        }

        if (!allowSplitTunneling) {
            // The tunnel scope comes from the protocol config (non-default
            // AllowedIPs): cutting the exclusions out of those routes keeps
            // the config untouched instead of widening the tunnel to
            // 0.0.0.0/0.
            val includes = routes.filter { it.include }.map { it.inetNetwork }.toSet()
            val keep = routes.filterNot { it.include }
            routes.clear()
            routes += keep
            routes += subtract(exclusions, includes).map { Route(it, true) }
            return
        }

        excludeAddresses(exclusions)
    }

    /**
     * Removes `exclusions` from `source` and returns the remaining prefixes.
     */
    private fun subtract(
        exclusions: Set<InetNetwork>,
        source: Set<InetNetwork>
    ): List<InetNetwork> {
        val ranges = IpRangeSet()
        source.forEach { ranges.add(IpRange(it)) }
        exclusions.forEach { ranges.remove(IpRange(it)) }
        return ranges.subnets()
    }

    protected fun ProtocolConfig.Builder.configAppSplitTunneling(config: JSONObject) {
        val splitTunnelType = config.optInt("appSplitTunnelType")
        if (splitTunnelType == SPLIT_TUNNEL_DISABLE) return
        val splitTunnelApps = config.getJSONArray("splitTunnelApps")
        val appHandlerFunc = when (splitTunnelType) {
            SPLIT_TUNNEL_INCLUDE -> ::includeApplication
            SPLIT_TUNNEL_EXCLUDE -> ::excludeApplication

            else -> throw BadConfigException("Unexpected value of the 'appSplitTunnelType' parameter: $splitTunnelType")
        }

        for (i in 0 until splitTunnelApps.length()) {
            appHandlerFunc(splitTunnelApps.getString(i))
        }
    }

    protected open fun buildVpnInterface(config: ProtocolConfig, vpnBuilder: Builder) {
        vpnBuilder.setSession(VPN_SESSION_NAME)

        for (addr in config.addresses) {
            Log.d(TAG, "addAddress: $addr")
            vpnBuilder.addAddress(addr)
        }

        for (addr in config.dnsServers) {
            Log.d(TAG, "addDnsServer: $addr")
            vpnBuilder.addDnsServer(addr)
        }
        // fix for Samsung android ignoring DNS servers outside the VPN route range
        if (Build.BRAND == "samsung") {
            for (addr in config.dnsServers) {
                Log.d(TAG, "addRoute: $addr")
                vpnBuilder.addRoute(InetNetwork(addr))
            }
        }

        config.searchDomain?.let {
            Log.d(TAG, "addSearchDomain: $it")
            vpnBuilder.addSearchDomain(it)
        }

        for ((inetNetwork, include) in config.routes) {
            if (include) {
                Log.d(TAG, "addRoute: $inetNetwork")
                vpnBuilder.addRoute(inetNetwork)
            } else {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    Log.d(TAG, "excludeRoute: $inetNetwork")
                    vpnBuilder.excludeRoute(inetNetwork)
                } else {
                    Log.e(TAG, "Trying to exclude route $inetNetwork on old Android")
                }
            }
        }

        for (app in config.includedApplications) {
            Log.d(TAG, "addAllowedApplication")
            vpnBuilder.addAllowedApplication(app)
        }

        for (app in config.excludedApplications) {
            Log.d(TAG, "addDisallowedApplication")
            vpnBuilder.addDisallowedApplication(app)
        }

        Log.d(TAG, "setMtu: ${config.mtu}")
        vpnBuilder.setMtu(config.mtu)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            config.httpProxy?.let {
                Log.d(TAG, "setHttpProxy: $it")
                vpnBuilder.setHttpProxy(it)
            }
        }

        if (config.allowAllAF) {
            Log.d(TAG, "allowFamily")
            vpnBuilder.allowFamily(OsConstants.AF_INET)
            vpnBuilder.allowFamily(OsConstants.AF_INET6)
        }

        Log.d(TAG, "setBlocking: ${config.blockingMode}")
        vpnBuilder.setBlocking(config.blockingMode)
        vpnBuilder.setUnderlyingNetworks(null)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            vpnBuilder.setMetered(false)
    }
}

private fun VpnService.Builder.addAddress(addr: InetNetwork) = addAddress(addr.address, addr.mask)
private fun VpnService.Builder.addRoute(addr: InetNetwork) = addRoute(addr.address, addr.mask)

@RequiresApi(Build.VERSION_CODES.TIRAMISU)
private fun VpnService.Builder.excludeRoute(addr: InetNetwork) = excludeRoute(IpPrefix(addr.address, addr.mask))
