package ru.outsidepro_arts.owalkie.flutter

import io.flutter.plugin.common.EventChannel

object PlatformEvents {
    const val EVENT_NOTIFICATION_DISCONNECT = "notification_disconnect"

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
}
