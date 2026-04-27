package ru.outsidepro_arts.owalkie.model

import android.content.Context

class RxVolumeStore(private val context: Context) {
    fun getPercent(): Int {
        val saved = prefs().getInt(KEY_RX_VOLUME_PERCENT, DEFAULT_RX_VOLUME_PERCENT)
        return saved.coerceIn(MIN_RX_VOLUME_PERCENT, MAX_RX_VOLUME_PERCENT)
    }

    fun setPercent(percent: Int) {
        val safe = percent.coerceIn(MIN_RX_VOLUME_PERCENT, MAX_RX_VOLUME_PERCENT)
        prefs().edit().putInt(KEY_RX_VOLUME_PERCENT, safe).apply()
    }

    private fun prefs() = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    companion object {
        private const val PREF_NAME = "audio_playback_config"
        private const val KEY_RX_VOLUME_PERCENT = "rx_volume_percent"

        const val MIN_RX_VOLUME_PERCENT = 0
        const val MAX_RX_VOLUME_PERCENT = 200
        const val DEFAULT_RX_VOLUME_PERCENT = 100
    }
}
