package ru.outsidepro_arts.owalkie

import org.json.JSONObject
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * Thin Kotlin wrapper around owalkie-core [SessionManager] via JNI.
 * Lifecycle and stale-event filtering are owned by the native library.
 */
class NativeRelayBridge(
    private val host: Host,
) : OwalkieNative.Listener {

    interface Host {
        fun applyWelcomeMessage(text: String)
        fun onRelaySessionReady(serverSessionId: Long)
        fun onRelayTransportLost()
        fun onRelayProtocolError()
        fun onRelayDisconnected()
        fun onRelayTxCountdownStart()
        fun onRelayTxStop()
        fun onRelayRxBroadcastStart(busyMode: Boolean)
        fun onRelayRxBroadcastEnd()
        fun onRelayPttLock(displaySec: Int)
        fun onRelayPttUnlock()
        fun onRelayRxOpus(opus: ByteArray)
        fun onRelayUdpReady(ready: Boolean)
        fun currentSignalByte(): Int
        fun isActivityFocused(): Boolean
    }

    @Volatile
    private var managedSessionId: Long = 0L
    private val connecting = AtomicBoolean(false)
    private val sessionReady = AtomicBoolean(false)
    private val udpReady = AtomicBoolean(false)
    @Volatile
    private var serverSessionId: Long = 0L

    val activeSessionId: Long
        get() = managedSessionId

    val isConnecting: Boolean
        get() = connecting.get()

    val isConnected: Boolean
        get() = sessionReady.get()

    val isUdpReady: Boolean
        get() = udpReady.get()

    val serverSessionIdOrZero: Long
        get() = serverSessionId

    fun connect(host: String, port: Int, channel: String, repeater: Boolean, wsSecure: Boolean): Boolean {
        if (wsSecure) {
            return false
        }
        haltForReconnect()
        OwalkieNative.ensureLoaded()
        connecting.set(true)
        val id = OwalkieNative.nativeConnect(this, host, port, channel, repeater)
        if (id == 0L) {
            connecting.set(false)
            return false
        }
        managedSessionId = id
        return true
    }

    /** Fast teardown before reconnect; does not emit disconnect UI events from core. */
    private fun haltForReconnect() {
        val id = managedSessionId
        if (id == 0L) {
            return
        }
        managedSessionId = 0L
        clearLocalState()
        OwalkieNative.nativeHaltAndRelease(id)
    }

    fun disconnect() {
        val id = managedSessionId
        if (id == 0L) {
            return
        }
        managedSessionId = 0L
        clearLocalState()
        OwalkieNative.nativeDisconnect(id)
    }

    fun disconnectAll() {
        val id = managedSessionId
        managedSessionId = 0L
        clearLocalState()
        if (id != 0L) {
            OwalkieNative.nativeDisconnect(id)
        }
        OwalkieNative.nativeDisconnectAll()
    }

    fun disconnectAllAndWait(timeoutMs: Int = 3000) {
        val id = managedSessionId
        managedSessionId = 0L
        clearLocalState()
        if (id != 0L) {
            OwalkieNative.nativeDisconnect(id)
        }
        OwalkieNative.nativeDisconnectAllAndWait(timeoutMs)
    }

    private fun clearLocalState() {
        managedSessionId = 0L
        connecting.set(false)
        sessionReady.set(false)
        udpReady.set(false)
        serverSessionId = 0L
    }

    fun sendTxOpus(opus: ByteArray, signal: Int): Boolean {
        val id = managedSessionId
        if (id == 0L || opus.isEmpty()) {
            return false
        }
        return OwalkieNative.nativeSendTxOpus(id, opus, signal) == OwalkieNative.OK
    }

    fun sendTxEof(): Boolean {
        val id = managedSessionId
        if (id == 0L) {
            return false
        }
        return OwalkieNative.nativeSendTxEof(id) == OwalkieNative.OK
    }

    fun setRepeater(enabled: Boolean) {
        val id = managedSessionId
        if (id == 0L) return
        OwalkieNative.nativeSetRepeater(id, enabled)
    }

    fun setPowerForeground() {
        val id = managedSessionId
        if (id == 0L) return
        OwalkieNative.nativeSetPowerProfile(id, OwalkieNative.POWER_FOREGROUND)
    }

    fun setPowerBackground() {
        val id = managedSessionId
        if (id == 0L) return
        OwalkieNative.nativeSetPowerProfile(id, OwalkieNative.POWER_BACKGROUND)
    }

    fun setPowerActiveTx() {
        val id = managedSessionId
        if (id == 0L) return
        OwalkieNative.nativeSetPowerProfile(id, OwalkieNative.POWER_ACTIVE_TX)
    }

    fun syncActivityFocus(focused: Boolean) {
        val id = managedSessionId
        if (id == 0L) return
        OwalkieNative.nativeSetActivityFocused(id, focused)
    }

    fun syncTxSignal() {
        val id = managedSessionId
        if (id == 0L) return
        OwalkieNative.nativeSetTxSignal(id, host.currentSignalByte())
    }

    fun notifyNetworkChanged() {
        val id = managedSessionId
        if (id == 0L) return
        OwalkieNative.nativeNotifyNetworkChanged(id)
    }

    override fun onNativeEvent(sessionId: Long, type: Int, info: String?) {
        if (sessionId != managedSessionId) {
            return
        }
        when (type) {
            OwalkieNative.EV_CONNECTING -> connecting.set(true)
            OwalkieNative.EV_CONNECTED -> Unit
            OwalkieNative.EV_DISCONNECTED, OwalkieNative.EV_CONNECT_FAILED -> {
                clearLocalState()
                host.onRelayTransportLost()
                host.onRelayDisconnected()
            }
            OwalkieNative.EV_PROTOCOL_ERROR -> {
                clearLocalState()
                host.onRelayProtocolError()
            }
            OwalkieNative.EV_WELCOME -> {
                if (!info.isNullOrBlank()) {
                    host.applyWelcomeMessage(info)
                }
            }
            OwalkieNative.EV_SESSION_READY -> {
                val serverSid = parseServerSessionId(info)
                if (serverSid != 0L) {
                    notifySessionReady(serverSid)
                }
            }
            OwalkieNative.EV_RX_BROADCAST_START -> {
                val busy = info?.toBooleanStrictOrNull() ?: false
                host.onRelayRxBroadcastStart(busy)
            }
            OwalkieNative.EV_RX_BROADCAST_END -> host.onRelayRxBroadcastEnd()
            OwalkieNative.EV_TX_COUNTDOWN_START -> host.onRelayTxCountdownStart()
            OwalkieNative.EV_TX_STOP -> host.onRelayTxStop()
            OwalkieNative.EV_PTT_LOCKED -> {
                val sec = info?.toIntOrNull() ?: 0
                host.onRelayPttLock(sec)
            }
            OwalkieNative.EV_PTT_UNLOCKED -> host.onRelayPttUnlock()
            OwalkieNative.EV_UDP_TRANSPORT_READY -> {
                udpReady.set(true)
                host.onRelayUdpReady(true)
            }
            OwalkieNative.EV_UDP_TRANSPORT_LOST -> {
                udpReady.set(false)
                host.onRelayUdpReady(false)
            }
        }
    }

    override fun onNativeRxOpus(sessionId: Long, opus: ByteArray) {
        if (sessionId != managedSessionId || opus.isEmpty()) {
            return
        }
        host.onRelayRxOpus(opus)
    }

    private fun parseServerSessionId(info: String?): Long {
        if (info.isNullOrBlank()) {
            return 0L
        }
        return runCatching { JSONObject(info).optLong("sessionId", 0L) }.getOrDefault(0L)
    }

    fun notifySessionReady(serverSessionId: Long) {
        connecting.set(false)
        sessionReady.set(true)
        this.serverSessionId = serverSessionId
        host.onRelaySessionReady(serverSessionId)
    }
}
