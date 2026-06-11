package ru.outsidepro_arts.owalkie.flutter

import android.os.Handler
import android.os.Looper
import io.flutter.plugin.common.EventChannel

object PlatformEvents {
    const val EVENT_NOTIFICATION_DISCONNECT = "notification_disconnect"
    const val EVENT_NETWORK_VALIDATED = "network_validated"
    const val EVENT_NETWORK_LOST = "network_lost"
    const val EVENT_MEDIA_PTT_TOGGLE = "media_ptt_toggle"
    const val EVENT_HARDWARE_PTT_DOWN = "hardware_ptt_down"
    const val EVENT_HARDWARE_PTT_UP = "hardware_ptt_up"
    const val EVENT_HARDWARE_PTT_BOUND = "hardware_ptt_bound"
    const val EVENT_EXTERNAL_PTT_DOWN = "external_ptt_down"
    const val EVENT_EXTERNAL_PTT_UP = "external_ptt_up"
    const val EVENT_EXTERNAL_PTT_TOGGLE = "external_ptt_toggle"
    const val EVENT_EXTERNAL_CALL_SIGNAL = "external_call_signal"
    const val EVENT_EXTERNAL_CONNECT = "external_connect"
    const val EVENT_EXTERNAL_DISCONNECT = "external_disconnect"
    const val EVENT_EXTERNAL_NEXT_CONNECTION = "external_next_connection"
    const val EVENT_EXTERNAL_PREVIOUS_CONNECTION = "external_previous_connection"

    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var sink: EventChannel.EventSink? = null

    fun attach(events: EventChannel.EventSink?) {
        sink = events
    }

    fun detach() {
        sink = null
    }

    fun emit(event: String) {
        mainHandler.post {
            sink?.success(event)
        }
    }

    fun emitSignalReport(mode: Int, value: Int) {
        emit("signal_report:$mode:$value")
    }

    fun emitSignalClear(mode: Int) {
        emit("signal_clear:$mode")
    }
}
