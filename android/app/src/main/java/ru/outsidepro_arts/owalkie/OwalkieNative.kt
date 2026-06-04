package ru.outsidepro_arts.owalkie



/**

 * JNI bridge to owalkie-core session manager.

 */

object OwalkieNative {

    const val EV_CONNECTING = 0

    const val EV_CONNECTED = 1

    const val EV_DISCONNECTED = 2

    const val EV_PROTOCOL_ERROR = 3

    const val EV_WELCOME = 4

    const val EV_RX_BROADCAST_START = 5

    const val EV_RX_BROADCAST_END = 6

    const val EV_LOCAL_TX_START = 7

    const val EV_LOCAL_TX_END = 8

    const val EV_PTT_LOCKED = 9

    const val EV_PTT_UNLOCKED = 10

    const val EV_TX_COUNTDOWN_START = 11

    const val EV_TX_STOP = 12

    const val EV_UDP_TRANSPORT_READY = 13

    const val EV_UDP_TRANSPORT_LOST = 14

    const val EV_CONNECT_FAILED = 15

    const val EV_SESSION_READY = 16



    const val POWER_FOREGROUND = 0

    const val POWER_BACKGROUND = 1

    const val POWER_ACTIVE_TX = 2



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

        fun onNativeRxOpus(sessionId: Long, opus: ByteArray)

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



    external fun nativeSetRepeater(sessionId: Long, enabled: Boolean)

    external fun nativeSendTxOpus(sessionId: Long, opus: ByteArray, signal: Int): Int

    external fun nativeSendTxEof(sessionId: Long): Int

    external fun nativeSetPowerProfile(sessionId: Long, profile: Int)

    external fun nativeSetTxSignal(sessionId: Long, signal: Int)

    external fun nativeNotifyNetworkChanged(sessionId: Long)

    external fun nativeSetActivityFocused(sessionId: Long, focused: Boolean)



    external fun nativeCreateOpusCodec(

        sampleRate: Int,

        packetMs: Int,

        bitrate: Int,

        complexity: Int,

        fec: Boolean,

        dtx: Boolean,

        application: String,

    ): Long



    external fun nativeDestroyOpusCodec(handle: Long)

    external fun nativeResetOpusCodec(handle: Long)

    external fun nativeOpusFrameSamples(handle: Long): Int

    external fun nativeOpusEncode(handle: Long, pcm: ShortArray): ByteArray?

    external fun nativeOpusDecode(handle: Long, opus: ByteArray, frameSamples: Int): ShortArray?

}


