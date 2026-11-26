package org.amnezia.vpn.protocol.xray

import android.content.Context
import android.net.VpnService.Builder
import java.io.File
import java.io.IOException
import go.Seq
import org.amnezia.vpn.protocol.BadConfigException
import org.amnezia.vpn.protocol.Protocol
import org.amnezia.vpn.protocol.ProtocolState.CONNECTED
import org.amnezia.vpn.protocol.ProtocolState.DISCONNECTED
import org.amnezia.vpn.protocol.Statistics
import org.amnezia.vpn.protocol.VpnStartException
import org.amnezia.vpn.protocol.xray.libXray.DialerController
import org.amnezia.vpn.protocol.xray.libXray.LibXray
import org.amnezia.vpn.protocol.xray.libXray.Logger
import org.amnezia.vpn.protocol.xray.libXray.Tun2SocksConfig
import org.amnezia.vpn.util.Log
import org.amnezia.vpn.util.net.InetNetwork
import org.amnezia.vpn.util.net.ip
import org.amnezia.vpn.util.net.parseInetAddress
import org.json.JSONObject

private const val TAG = "Xray"
private const val LIBXRAY_TAG = "libXray"

class Xray : Protocol() {

    private var isRunning: Boolean = false

    // libXray exposes no byte counters, so read the VpnService tun interface
    // stats instead — Android allows only one active VPN, "tun0" is ours while
    // connected (same interface name the notification TrafficStats helper uses)
    override val statistics: Statistics
        get() {
            if (!isRunning) return Statistics.EMPTY_STATISTICS
            val rx = android.net.TrafficStats.getRxBytes("tun0")
            val tx = android.net.TrafficStats.getTxBytes("tun0")
            if (rx == android.net.TrafficStats.UNSUPPORTED.toLong() ||
                tx == android.net.TrafficStats.UNSUPPORTED.toLong()
            ) {
                return Statistics.EMPTY_STATISTICS
            }
            return Statistics.build {
                setRxBytes(rx)
                setTxBytes(tx)
            }
        }

    override fun internalInit() {
        Seq.setContext(context)
        if (!isInitialized) {
            LibXray.initLogger(object : Logger {
                override fun warning(s: String) = Log.w(LIBXRAY_TAG, s)

                override fun error(s: String) = Log.e(LIBXRAY_TAG, s)

                override fun write(msg: ByteArray): Long {
                    Log.w(LIBXRAY_TAG, String(msg))
                    return msg.size.toLong()
                }
            }).isNotNullOrBlank { err ->
                Log.w(TAG, "Failed to initialize logger: $err")
            }
        }
    }

    override suspend fun startVpn(config: JSONObject, vpnBuilder: Builder, protect: (Int) -> Boolean) {
        if (isRunning) {
            Log.w(TAG, "XRay already running")
            return
        }

        val xrayJsonConfig = config.optJSONObject("xray_config_data")
            ?: config.optJSONObject("ssxray_config_data")
            ?: throw BadConfigException("config_data not found")
        val xrayConfig = parseConfig(config, xrayJsonConfig)

        (xrayJsonConfig.optJSONObject("log") ?: JSONObject().also { xrayJsonConfig.put("log", it) })
            .put("loglevel", "warning")
            .put("access", "none") // disable access log

        // The endpoint must be dialled by address: resolving it inside xray would
        // ask the system resolver while the tunnel is already up.
        //
        // This used to be a blind `json.replace(hostName, ip)` over the whole
        // config, which also rewrote `serverName` — the TLS SNI of a hysteria2
        // card. A node reached by name then presented its certificate for the
        // name while the client asked for the IP, so the QUIC handshake died
        // after the first flight: the node answered every packet and the client
        // dropped every reply, retrying on a fresh socket each time. Only the
        // address fields are pinned here; the TLS name stays exactly as the
        // card states it.
        val hostName = config.optString("hostName")
        if (hostName.isNotEmpty()) {
            val ipAddress = parseInetAddress(hostName).ip
            if (hostName != ipAddress) {
                pinEndpointAddress(xrayJsonConfig, hostName, ipAddress)
            }
        }
        val xrayJsonConfigString = xrayJsonConfig.toString()

        start(xrayConfig, xrayJsonConfigString, vpnBuilder, protect)
        state.value = CONNECTED
        isRunning = true
    }

    // Rewrite the dial address of every outbound from `hostName` to `ip`,
    // leaving `serverName`/`sni` untouched: those must keep the name the
    // certificate was issued for.
    private fun pinEndpointAddress(config: JSONObject, hostName: String, ip: String) {
        fun rewrite(target: JSONObject) {
            if (target.optString("address") == hostName) {
                target.put("address", ip)
            }
        }

        val outbounds = config.optJSONArray("outbounds") ?: return
        for (i in 0 until outbounds.length()) {
            val settings = outbounds.optJSONObject(i)?.optJSONObject("settings") ?: continue
            rewrite(settings)
            for (key in listOf("servers", "vnext")) {
                val entries = settings.optJSONArray(key) ?: continue
                for (j in 0 until entries.length()) {
                    entries.optJSONObject(j)?.let { rewrite(it) }
                }
            }
        }
    }

    private fun parseConfig(config: JSONObject, xrayJsonConfig: JSONObject): XrayConfig {
        return XrayConfig.build {
            addAddress(XrayConfig.DEFAULT_IPV4_ADDRESS)

            config.optString("dns1").let {
                if (it.isNotBlank()) addDnsServer(parseInetAddress(it))
            }

            config.optString("dns2").let {
                if (it.isNotBlank()) addDnsServer(parseInetAddress(it))
            }

            addRoute(InetNetwork("0.0.0.0", 0))
            // no 2000::/3: our nodes are IPv4-only — a v6 default route into
            // the tunnel blackholes IPv6-preferred apps (YouTube on Android)
            config.getString("hostName").let {
                excludeRoute(InetNetwork(it, 32))
            }

            config.optString("mtu").let {
                if (it.isNotBlank()) setMtu(it.toInt())
            }

            val socksConfig = xrayJsonConfig.getJSONArray("inbounds")[0] as JSONObject
            socksConfig.getInt("port").let { setSocksPort(it) }

            configSplitTunneling(config)
            configLocalSubnetExclusions(config)
            configAppSplitTunneling(config)
        }
    }

    private fun start(config: XrayConfig, configJson: String, vpnBuilder: Builder, protect: (Int) -> Boolean) {
        buildVpnInterface(config, vpnBuilder)

        DialerController { protect(it.toInt()) }.also {
            LibXray.registerDialerController(it).isNotNullOrBlank { err ->
                throw VpnStartException("Failed to register dialer controller: $err")
            }
            LibXray.registerListenerController(it).isNotNullOrBlank { err ->
                throw VpnStartException("Failed to register listener controller: $err")
            }
        }

        vpnBuilder.establish().use { tunFd ->
            if (tunFd == null) {
                throw VpnStartException("Create VPN interface: permission not granted or revoked")
            }
            Log.d(TAG, "Run tun2Socks")
            runTun2Socks(config, tunFd.detachFd())

            Log.d(TAG, "Run XRay")
            Log.i(TAG, "xray ${LibXray.xrayVersion()}")
            val assetsPath = context.getDir("assets", Context.MODE_PRIVATE).absolutePath
            LibXray.initXray(assetsPath)
            val geoDir = File(assetsPath, "geo").absolutePath
            val configPath = File(context.cacheDir, "config.json")
            Log.v(TAG, "xray.location.asset: $geoDir")
            Log.v(TAG, "config: $configPath")
            try {
                configPath.writeText(configJson)
            } catch (e: IOException) {
                LibXray.stopTun2Socks()
                throw VpnStartException("Failed to write xray config: ${e.message}")
            }
            LibXray.runXray(geoDir, configPath.absolutePath, config.maxMemory).isNotNullOrBlank { err ->
                LibXray.stopTun2Socks()
                throw VpnStartException("Failed to start xray: $err")
            }
        }
    }

    override fun stopVpn() {
        LibXray.stopXray().isNotNullOrBlank { err ->
            Log.e(TAG, "Failed to stop XRay: $err")
        }
        LibXray.stopTun2Socks().isNotNullOrBlank { err ->
            Log.e(TAG, "Failed to stop tun2Socks: $err")
        }

        isRunning = false
        state.value = DISCONNECTED
    }

    override fun reconnectVpn(vpnBuilder: Builder, protect: (Int) -> Boolean) {
        state.value = CONNECTED
    }

    private fun runTun2Socks(config: XrayConfig, fd: Int) {
        val tun2SocksConfig = Tun2SocksConfig().apply {
            mtu = config.mtu.toLong()
            proxy = "socks5://127.0.0.1:${config.socksPort}"
            device = "fd://$fd"
            logLevel = "warn"
        }
        LibXray.startTun2Socks(tun2SocksConfig, fd.toLong()).isNotNullOrBlank { err ->
            throw VpnStartException("Failed to start tun2socks: $err")
        }
    }

    companion object {
        val instance: Xray by lazy { Xray() }
    }
}

private fun String?.isNotNullOrBlank(block: (String) -> Unit) {
    if (!this.isNullOrBlank()) {
        block(this)
    }
}
