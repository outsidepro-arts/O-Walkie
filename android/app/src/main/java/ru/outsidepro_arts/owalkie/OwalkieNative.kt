package ru.outsidepro_arts.owalkie



/**

 * JNI bridge to owalkie-core session manager.

 */

object OwalkieNative {

    const val EV_READY = 0
    const val EV_DISCONNECTED = 1
    const val EV_PROTOCOL_ERROR = 2
    const val EV_CONNECT_FAILED = 3
    const val EV_RX_BROADCAST_START = 4
    const val EV_RX_BROADCAST_END = 5
    const val EV_PTT_LOCKED = 6
    const val EV_PTT_UNLOCKED = 7
    const val EV_TX_COUNTDOWN_START = 8
    const val EV_TX_STOP = 9



    const val POWER_FOREGROUND = 0

    const val POWER_BACKGROUND = 1

    const val POWER_ACTIVE_TX = 2

    const val SIGNAL_WIFI = 0
    const val SIGNAL_CELL = 1

    const val OK = 0

    const val ERR_NOT_READY = 9



    private var loaded = false



    fun ensureLoaded() {

        if (loaded) return

        System.loadLibrary("owalkie_jni")

        loaded = true

    }



    interface Listener {

        fun onNativeEvent(sessionId: Long, type: Int, info: String?)

        fun onNativeRxPcm(sessionId: Long, pcm: ShortArray, sampleRate: Int, packetMs: Int)

    }



    external fun nativeConnect(

        listener: Listener,

        host: String,

        port: Int,

        channel: String,

        repeater: Boolean,

    ): Long



    external fun nativeDisconnect(sessionId: Long)

    /** Halt transport without disconnect events (reconnect / service teardown). */
    external fun nativeHaltAndRelease(sessionId: Long)

    external fun nativeDisconnectAll()

    external fun nativeDisconnectAllAndWait(timeoutMs: Int)

    external fun nativeSessionValid(sessionId: Long): Int

    external fun nativeSessionReady(sessionId: Long): Int

    external fun nativeGetSessionInfoFlags(sessionId: Long, out: IntArray): Int

    /** Fills @p out (10 ints) and returns negotiated Opus application, or null on error. */
    external fun nativeGetSessionInfoConfig(sessionId: Long, out: IntArray): String?

    data class SessionInfo(
        val ready: Boolean,
        val connected: Boolean,
        val udpReady: Boolean,
        val receiving: Boolean,
        val localTxActive: Boolean,
        val pttServerLocked: Boolean,
        val pttLockDisplaySec: Int,
        val serverSessionId: Long,
        val protocolVersion: Int,
        val sampleRate: Int,
        val packetMs: Int,
        val busyMode: Boolean,
        val transmitTimeoutSec: Int,
        val opusBitrate: Int,
        val opusComplexity: Int,
        val opusFec: Boolean,
        val opusDtx: Boolean,
        val opusApplication: String,
    )

    fun getSessionInfo(sessionId: Long): SessionInfo? {
        if (sessionId == 0L) {
            return null
        }
        val flags = IntArray(8)
        if (nativeGetSessionInfoFlags(sessionId, flags) != OK) {
            return null
        }
        if (flags[7] == 0) {
            return null
        }
        val config = IntArray(10)
        val opusApp = nativeGetSessionInfoConfig(sessionId, config) ?: return null
        return SessionInfo(
            ready = flags[0] != 0,
            connected = flags[1] != 0,
            udpReady = flags[2] != 0,
            receiving = flags[3] != 0,
            localTxActive = flags[4] != 0,
            pttServerLocked = flags[5] != 0,
            pttLockDisplaySec = flags[6],
            serverSessionId = config[0].toLong() and 0xFFFFFFFFL,
            protocolVersion = config[1],
            sampleRate = config[2],
            packetMs = config[3],
            busyMode = config[4] != 0,
            transmitTimeoutSec = config[5],
            opusBitrate = config[6],
            opusComplexity = config[7],
            opusFec = config[8] != 0,
            opusDtx = config[9] != 0,
            opusApplication = opusApp,
        )
    }

    external fun nativeSetRepeater(sessionId: Long, enabled: Boolean)

    external fun nativeTxStart(sessionId: Long): Int

    external fun nativePushTxPcm(sessionId: Long, pcm: ShortArray): Int

    external fun nativeTxEnd(sessionId: Long): Int

    external fun nativeSetPowerProfile(sessionId: Long, profile: Int)

    external fun nativePunchNat(sessionId: Long): Int

    external fun nativeReportSignal(mode: Int, value: Int): Int

    external fun nativeClearSignal(mode: Int): Int

    external fun nativeGetUplinkSignalByte(): Int

    external fun nativeSetActivityFocused(sessionId: Long, focused: Boolean)
}


