package ru.outsidepro_arts.owalkie.flutter

import io.flutter.plugin.common.EventChannel

object PlatformEvents {
    const val EVENT_NOTIFICATION_DISCONNECT = "notification_disconnect"
    const val EVENT_NETWORK_VALIDATED = "network_validated"
    const val EVENT_NETWORK_LOST = "network_lost"

    @Volatile
    private var sink: EventChannel.EventSink? = null

    fun attach(events: EventChannel.EventSink?) {
        sink = events
    }

    fun detach() {
        sink = null
    }

    fun emit(event: String) {
        sink?.success(event)
    }

    fun emitSignalReport(mode: Int, value: Int) {
        emit("signal_report:$mode:$value")
    }

    fun emitSignalClear(mode: Int) {
        emit("signal_clear:$mode")
    }
}
