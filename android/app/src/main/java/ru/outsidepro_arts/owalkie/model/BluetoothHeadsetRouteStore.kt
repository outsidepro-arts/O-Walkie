package ru.outsidepro_arts.owalkie.model

import android.content.Context

class BluetoothHeadsetRouteStore(context: Context) {
    private val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    fun isEnabled(): Boolean = prefs.getBoolean(KEY_ENABLED, false)

    fun setEnabled(enabled: Boolean) {
        prefs.edit().putBoolean(KEY_ENABLED, enabled).apply()
    }

    companion object {
        private const val PREF_NAME = "bluetooth_headset_route"
        private const val KEY_ENABLED = "enabled"
    }
}
