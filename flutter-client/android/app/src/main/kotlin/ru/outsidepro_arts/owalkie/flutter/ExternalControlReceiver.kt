package ru.outsidepro_arts.owalkie.flutter

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import androidx.core.content.ContextCompat

/**
 * Open broadcast API for automation apps (Tasker/MacroDroid). Same action strings as the
 * Kotlin Android client ([ru.outsidepro_arts.owalkie.ExternalControlReceiver]).
 */
class ExternalControlReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        val action = intent?.action ?: return
        if (!ExternalControlStore(context).isEnabled()) {
            return
        }
        val serviceIntent = Intent(context, WalkieForegroundService::class.java).apply {
            this.action = action
            if (intent.extras != null) {
                putExtras(intent.extras!!)
            }
        }
        ContextCompat.startForegroundService(context, serviceIntent)
    }

    companion object {
        const val ACTION_PTT_DOWN = "ru.outsidepro_arts.owalkie.api.PTT_DOWN"
        const val ACTION_PTT_UP = "ru.outsidepro_arts.owalkie.api.PTT_UP"
        const val ACTION_PTT_TOGGLE = "ru.outsidepro_arts.owalkie.api.PTT_TOGGLE"
        const val ACTION_CALL_SIGNAL = "ru.outsidepro_arts.owalkie.api.CALL_SIGNAL"
        const val ACTION_CONNECT = "ru.outsidepro_arts.owalkie.api.CONNECT"
        const val ACTION_DISCONNECT = "ru.outsidepro_arts.owalkie.api.DISCONNECT"
        const val ACTION_NEXT_CONNECTION = "ru.outsidepro_arts.owalkie.api.NEXT_CONNECTION"
        const val ACTION_PREVIOUS_CONNECTION = "ru.outsidepro_arts.owalkie.api.PREVIOUS_CONNECTION"
    }
}
