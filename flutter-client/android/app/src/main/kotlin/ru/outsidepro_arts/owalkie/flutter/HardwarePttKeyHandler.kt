package ru.outsidepro_arts.owalkie.flutter

import android.content.Context
import android.view.KeyEvent

/**
 * Routes assigned hardware PTT keys to [PlatformEvents] (hold policy).
 */
object HardwarePttKeyHandler {
    fun tryCaptureBinding(
        context: Context,
        event: KeyEvent,
        onCaptured: () -> Unit,
    ): Boolean {
        if (event.action != KeyEvent.ACTION_DOWN || event.repeatCount != 0) {
            return false
        }
        PttHardwareKeyStore(context).setBinding(
            PttHardwareKeyStore.Binding(
                keyCode = event.keyCode,
                scanCode = event.scanCode,
            ),
        )
        onCaptured()
        return true
    }

    fun tryHandlePtt(context: Context, event: KeyEvent): Boolean {
        if (!PttHardwareKeyStore(context).matches(event)) {
            return false
        }
        when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                if (event.repeatCount == 0) {
                    PlatformEvents.emit(PlatformEvents.EVENT_HARDWARE_PTT_DOWN)
                }
            }
            KeyEvent.ACTION_UP -> {
                PlatformEvents.emit(PlatformEvents.EVENT_HARDWARE_PTT_UP)
            }
            else -> return false
        }
        return true
    }
}
