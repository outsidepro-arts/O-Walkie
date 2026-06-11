package ru.outsidepro_arts.owalkie.flutter

import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper

object PttMediaSessionHost {
    private val mainHandler = Handler(Looper.getMainLooper())
    private var controller: PttMediaSessionController? = null

    fun sync(context: Context, active: Boolean) {
        if (!active) {
            controller?.release()
            controller = null
            return
        }
        val ctrl = controller ?: PttMediaSessionController(context.applicationContext, mainHandler) {
            PlatformEvents.emit(PlatformEvents.EVENT_MEDIA_PTT_TOGGLE)
        }.also { controller = it }
        ctrl.ensureActiveAndRefreshState()
    }

    fun dispatchMediaButtonIntent(intent: Intent): Boolean {
        return controller?.dispatchMediaButtonIntent(intent) ?: false
    }

    fun release() {
        controller?.release()
        controller = null
    }
}
