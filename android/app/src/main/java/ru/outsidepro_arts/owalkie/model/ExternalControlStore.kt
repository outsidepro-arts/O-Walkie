package ru.outsidepro_arts.owalkie.model

import android.content.Context

class ExternalControlStore(context: Context) {
    private val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    fun isEnabled(): Boolean = prefs.getBoolean(KEY_EXTERNAL_CONTROL_ENABLED, false)

    fun setEnabled(enabled: Boolean) {
        prefs.edit()
            .putBoolean(KEY_EXTERNAL_CONTROL_ENABLED, enabled)
            .apply()
    }

    companion object {
        private const val PREF_NAME = "external_control_config"
        private const val KEY_EXTERNAL_CONTROL_ENABLED = "external_control_enabled"
    }
}
