package ru.outsidepro_arts.owalkie

import android.os.Handler
import android.os.Looper
import android.util.Log
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Thin Kotlin wrapper around owalkie-core via JNI.
 * [prepareConnection] allocates session id; [activateConnection] is driven by [WalkieService].
 */
class NativeRelayBridge(
    private val host: Host,
) : OwalkieNative.Listener {

    companion object {
        private const val TAG = "OwalkieRelay"
        private val nativeEventHandler = Handler(Looper.getMainLooper())
        private val nativeRxPcmExecutor = Executors.newSingleThreadExecutor { runnable ->
            Thread(runnable, "owalkie-rx-pcm").apply { priority = Thread.NORM_PRIORITY + 1 }
        }
    }

    interface Host {
        fun applyWelcomeFromSessionInfo(info: OwalkieNative.SessionInfo)
        fun onRelayConnected(serverSessionId: Long, udpReady: Boolean)
        fun onRelayConnectionLost(reason: String?)
        fun onRelayProtocolError()
        fun onRelayDisconnected()
        fun onRelayTxCountdownStart()
        fun onRelayTxStop()
        fun onRelayRxBroadcastStart(busyMode: Boolean)
        fun onRelayRxBroadcastEnd()
        fun onRelayPttLock(displaySec: Int)
        fun onRelayPttUnlock()
        fun onRelayRxPcm(pcm: ShortArray, sampleRate: Int, packetMs: Int)
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

    fun detachLocalTransportState(): Long {
        val id = managedSessionId
        clearLocalState()
        return id
    }

    fun prepareConnection(host: String, port: Int, channel: String, repeater: Boolean, wsSecure: Boolean): Boolean {
        if (wsSecure) {
            return false
        }
        OwalkieNative.ensureLoaded()
        val existingId = managedSessionId
        if (existingId != 0L) {
            if (OwalkieNative.nativeSessionValid(existingId) != 0) {
                if (OwalkieNative.nativeSessionReady(existingId) != 0) {
                    Log.i(TAG, "prepareConnection: reuse session=$existingId")
                    connecting.set(!sessionReady.get())
                    return true
                }
                Log.w(TAG, "prepareConnection: drop in-flight session=$existingId")
                OwalkieNative.nativeDisconnect(existingId)
                managedSessionId = 0L
                clearLocalState()
            } else {
                Log.w(TAG, "prepareConnection: drop stale kotlin session=$existingId")
                managedSessionId = 0L
                clearLocalState()
            }
        }
        connecting.set(true)
        val id = OwalkieNative.nativePrepareConnection(this, host, port, channel, repeater)
        if (id == 0L) {
            connecting.set(false)
            return false
        }
        managedSessionId = id
        return true
    }

    fun activateConnection(timeoutMs: Int): Int {
        val id = managedSessionId
        if (id == 0L) {
            return OwalkieNative.ERR_NOT_READY
        }
        return OwalkieNative.nativeConnect(id, timeoutMs)
    }

    fun disconnect() {
        val id = managedSessionId
        managedSessionId = 0L
        clearLocalState()
        if (id == 0L) {
            return
        }
        Log.i(TAG, "disconnect session=$id")
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

    fun txOpen(): Boolean = txSubmit(OwalkieNative.TX_OPEN, null)

    fun txPcm(pcm: ShortArray): Boolean = txSubmit(OwalkieNative.TX_PCM, pcm)

    fun txVoiceEnd(): Boolean = txSubmit(OwalkieNative.TX_VOICE_END, null)

    fun txClose(): Boolean = txSubmit(OwalkieNative.TX_CLOSE, null)

    fun txAbort(): Boolean = txSubmit(OwalkieNative.TX_ABORT, null)

    fun waitTxIdle(timeoutMs: Int = 500): Boolean {
        val id = managedSessionId
        if (id == 0L) return true
        return OwalkieNative.nativeTxWaitIdle(id, timeoutMs)
    }

    private fun txSubmit(op: Int, pcm: ShortArray?): Boolean {
        val id = managedSessionId
        if (id == 0L) return false
        return OwalkieNative.nativeTxSubmit(id, op, pcm) == OwalkieNative.OK
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

    fun punchNat(): Boolean {
        val id = managedSessionId
        if (id == 0L) return false
        return OwalkieNative.nativePunchNat(id) == OwalkieNative.OK
    }

    fun isNativeSessionReady(): Boolean {
        val id = managedSessionId
        return id != 0L &&
            OwalkieNative.nativeSessionValid(id) != 0 &&
            OwalkieNative.nativeSessionReady(id) != 0
    }

    /**
     * Align Kotlin transport flags with owalkie-core (source of truth).
     * RX/TX can keep working via JNI while [sessionReady] was cleared by a stale event.
     */
    fun syncTransportStateFromNative(): Boolean {
        val id = managedSessionId
        if (id == 0L || OwalkieNative.nativeSessionValid(id) == 0) {
            return false
        }
        val nativeReady = OwalkieNative.nativeSessionReady(id) != 0
        val kotlinReady = sessionReady.get()
        if (nativeReady && !kotlinReady) {
            return promoteReadyFromNative(id)
        }
        if (!nativeReady && kotlinReady) {
            Log.w(TAG, "syncTransport: clearing stale kotlin ready session=$id")
            sessionReady.set(false)
            connecting.set(true)
            udpReady.set(false)
            return true
        }
        if (nativeReady) {
            val sessionInfo = OwalkieNative.getSessionInfo(id) ?: return false
            var changed = false
            if (sessionInfo.udpReady != udpReady.get()) {
                udpReady.set(sessionInfo.udpReady)
                changed = true
            }
            if (sessionInfo.serverSessionId != 0L && sessionInfo.serverSessionId != serverSessionId) {
                serverSessionId = sessionInfo.serverSessionId
                host.applyWelcomeFromSessionInfo(sessionInfo)
                changed = true
            }
            return changed
        }
        return false
    }

    fun resyncReadyFromNative(): Boolean {
        val id = managedSessionId
        if (id == 0L || sessionReady.get()) {
            return false
        }
        if (!isNativeSessionReady()) {
            return false
        }
        return promoteReadyFromNative(id)
    }

    private fun promoteReadyFromNative(sessionId: Long): Boolean {
        val sessionInfo = OwalkieNative.getSessionInfo(sessionId)
        Log.i(TAG, "promoteReadyFromNative session=$sessionId hasInfo=${sessionInfo != null}")
        if (sessionInfo != null) {
            host.applyWelcomeFromSessionInfo(sessionInfo)
            notifyRelayConnected(sessionInfo.serverSessionId, sessionInfo.udpReady)
        } else {
            notifyRelayConnected(0L, false)
        }
        return true
    }

    override fun onNativeEvent(sessionId: Long, type: Int, info: String?) {
        nativeEventHandler.post { dispatchNativeEvent(sessionId, type, info) }
    }

    private fun dispatchNativeEvent(sessionId: Long, type: Int, info: String?) {
        if (sessionId != managedSessionId) {
            Log.w(TAG, "stale event session=$sessionId current=$managedSessionId type=$type")
            return
        }
        Log.i(TAG, "onNativeEvent session=$sessionId type=$type info=$info")
        when (type) {
            OwalkieNative.EV_CONNECTED -> {
                val sessionInfo = OwalkieNative.getSessionInfo(managedSessionId)
                if (sessionInfo != null) {
                    host.applyWelcomeFromSessionInfo(sessionInfo)
                    udpReady.set(sessionInfo.udpReady)
                    if (sessionInfo.serverSessionId != 0L) {
                        notifyRelayConnected(sessionInfo.serverSessionId, sessionInfo.udpReady)
                    } else {
                        notifyRelayConnected(0L, sessionInfo.udpReady)
                    }
                } else if (OwalkieNative.nativeSessionReady(managedSessionId) != 0) {
                    Log.w(TAG, "EV_CONNECTED without sessionInfo; using nativeSessionReady fallback")
                    notifyRelayConnected(0L, false)
                } else {
                    Log.w(TAG, "EV_CONNECTED but session not ready in core")
                }
            }
            OwalkieNative.EV_CONNECTION_LOST -> {
                if (isNativeSessionReady()) {
                    Log.i(TAG, "CONNECTION_LOST ignored; native still ready info=$info")
                    syncTransportStateFromNative()
                    return
                }
                connecting.set(true)
                sessionReady.set(false)
                udpReady.set(false)
                host.onRelayConnectionLost(info)
            }
            OwalkieNative.EV_DISCONNECTED -> {
                connecting.set(false)
                sessionReady.set(false)
                udpReady.set(false)
                serverSessionId = 0L
                host.onRelayDisconnected()
            }
            OwalkieNative.EV_CONNECTION_FAILED -> {
                clearLocalState()
                host.onRelayDisconnected()
            }
            OwalkieNative.EV_PROTOCOL_ERROR -> {
                clearLocalState()
                host.onRelayProtocolError()
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
        }
    }

    override fun onNativeRxPcm(sessionId: Long, pcm: ShortArray, sampleRate: Int, packetMs: Int) {
        if (sessionId != managedSessionId || pcm.isEmpty()) {
            return
        }
        val copy = pcm.copyOf()
        nativeRxPcmExecutor.execute {
            if (sessionId != managedSessionId) {
                return@execute
            }
            host.onRelayRxPcm(copy, sampleRate, packetMs)
        }
    }

    private fun notifyRelayConnected(serverSessionId: Long, udpReady: Boolean) {
        connecting.set(false)
        sessionReady.set(true)
        this.serverSessionId = serverSessionId
        this.udpReady.set(udpReady)
        host.onRelayConnected(serverSessionId, udpReady)
    }
}
