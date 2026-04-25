package com.owalkie.app.model

import android.content.Context
import android.view.KeyEvent

class PttHardwareKeyStore(context: Context) {
    private val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    fun getAssignedKeyCode(): Int {
        val value = prefs.getInt(KEY_ASSIGNED_KEY_CODE, KeyEvent.KEYCODE_UNKNOWN)
        return if (value > KeyEvent.KEYCODE_UNKNOWN) value else KeyEvent.KEYCODE_UNKNOWN
    }

    fun setAssignedKeyCode(keyCode: Int) {
        val safe = if (keyCode > KeyEvent.KEYCODE_UNKNOWN) keyCode else KeyEvent.KEYCODE_UNKNOWN
        prefs.edit().putInt(KEY_ASSIGNED_KEY_CODE, safe).apply()
    }

    companion object {
        private const val PREF_NAME = "ptt_hardware_key_config"
        private const val KEY_ASSIGNED_KEY_CODE = "assigned_key_code"
    }
}
