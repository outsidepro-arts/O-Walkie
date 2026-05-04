package ru.outsidepro_arts.owalkie.model

import android.content.Context

/** When enabled, [WalkieService] tears down relay transport during an active cellular call and reconnects after the call ends. */
class PhoneCallRelayPauseStore(context: Context) {
    private val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    fun isPauseDuringCallEnabled(): Boolean = prefs.getBoolean(KEY_PAUSE, DEFAULT_PAUSE)

    fun setPauseDuringCallEnabled(enabled: Boolean) {
        prefs.edit().putBoolean(KEY_PAUSE, enabled).apply()
    }

    companion object {
        private const val PREF_NAME = "phone_call_relay_pause"
        private const val KEY_PAUSE = "pause_during_cellular_call"
        private const val DEFAULT_PAUSE = true
    }
}
