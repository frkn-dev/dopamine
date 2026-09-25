package org.amnezia.vpn.protocol.wireguard

import android.content.Context
import android.os.Build
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.amnezia.vpn.protocol.VpnStartException
import org.amnezia.vpn.util.Log

private const val TAG = "VkTurnSidecar"
private const val ASSET_DIR = "vkturn"
private const val READY_TIMEOUT_MS = 25_000L

object VkTurnSidecar {

    private val processRef = AtomicReference<Process?>(null)

    val excludeCidrs: List<String> = listOf(
        "155.212.192.0/20",
        "87.240.128.0/18",
        "93.186.224.0/20",
        "95.142.192.0/20",
        "185.32.184.0/22",
        "185.100.104.0/22",
    )

    fun start(
        context: Context,
        peerHost: String,
        peerPort: Int,
        callLink: String,
        localPort: Int,
        streams: Int,
    ) {
        stop()
        val link = callLink.trim()
        if (link.isEmpty()) {
            throw VpnStartException("VK TURN: call link is empty")
        }
        if (peerHost.isBlank()) {
            throw VpnStartException("VK TURN: peer host is empty")
        }
        val binary = ensureBinary(context)
        val cmd = listOf(
            binary.absolutePath,
            "-listen", "127.0.0.1:$localPort",
            "-peer", "$peerHost:$peerPort",
            "-links", link,
            "-n", streams.coerceIn(1, 12).toString(),
            "-mode", "udp",
            "-provider", "vk",
            "-platform", "mobile",
            "-transport", "tcp",
        )
        Log.i(TAG, "Starting: ${cmd.joinToString(" ").replace(link, "<link>")}")
        val process = ProcessBuilder(cmd)
            .directory(binary.parentFile)
            .redirectErrorStream(true)
            .start()
        processRef.set(process)
        Thread({
            process.inputStream.bufferedReader().useLines { lines ->
                lines.forEach { line -> Log.i(TAG, line) }
            }
        }, "vkturn-log").apply { isDaemon = true; start() }

        val deadline = System.currentTimeMillis() + READY_TIMEOUT_MS
        while (System.currentTimeMillis() < deadline) {
            if (!process.isAlive) {
                processRef.compareAndSet(process, null)
                throw VpnStartException("VK TURN: sidecar exited early")
            }
            if (isLocalUdpOpen(localPort)) {
                Log.i(TAG, "Local listen ready on $localPort")
                return
            }
            Thread.sleep(200)
        }
        stop()
        throw VpnStartException("VK TURN: sidecar listen timeout")
    }

    fun stop() {
        val process = processRef.getAndSet(null) ?: return
        process.destroy()
        if (!process.waitFor(2, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            process.waitFor(1, TimeUnit.SECONDS)
        }
        Log.i(TAG, "Stopped")
    }

    private fun ensureBinary(context: Context): File {
        val abi = preferredAbi()
        val assetName = "$ASSET_DIR/client-android-$abi"
        val outDir = File(context.filesDir, "vkturn").apply { mkdirs() }
        val out = File(outDir, "client-android-$abi")
        context.assets.open(assetName).use { input ->
            FileOutputStream(out).use { output -> input.copyTo(output) }
        }
        out.setExecutable(true, false)
        if (!out.canExecute()) {
            throw VpnStartException("VK TURN: cannot chmod sidecar binary")
        }
        return out
    }

    private fun preferredAbi(): String {
        val abis = Build.SUPPORTED_ABIS
        if (abis.any { it.startsWith("arm64") }) return "arm64"
        if (abis.any { it.startsWith("armeabi") }) return "armeabi-v7a"
        throw VpnStartException("VK TURN: unsupported ABI ${abis.joinToString()}")
    }

    private fun isLocalUdpOpen(port: Int): Boolean {
        return try {
            java.net.DatagramSocket(null).use { socket ->
                socket.reuseAddress = false
                socket.bind(java.net.InetSocketAddress("127.0.0.1", port))
            }
            false
        } catch (_: java.net.BindException) {
            true
        } catch (_: Exception) {
            false
        }
    }
}
