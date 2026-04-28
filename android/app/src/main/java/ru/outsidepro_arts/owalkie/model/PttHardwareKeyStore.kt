package ru.outsidepro_arts.owalkie.model

import android.content.Context
import android.view.KeyEvent

class PttHardwareKeyStore(context: Context) {
    private val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    data class Binding(
        val keyCode: Int = KeyEvent.KEYCODE_UNKNOWN,
        val scanCode: Int = 0,
        val handleInBackground: Boolean = false,
    ) {
        fun isAssigned(): Boolean = keyCode != KeyEvent.KEYCODE_UNKNOWN || scanCode > 0
    }

    fun getAssignedKeyCode(): Int {
        val value = getBinding().keyCode
        return if (value > KeyEvent.KEYCODE_UNKNOWN) value else KeyEvent.KEYCODE_UNKNOWN
    }

    fun setAssignedKeyCode(keyCode: Int) {
        val safe = if (keyCode > KeyEvent.KEYCODE_UNKNOWN) keyCode else KeyEvent.KEYCODE_UNKNOWN
        prefs.edit()
            .putInt(KEY_ASSIGNED_KEY_CODE, safe)
            .putInt(KEY_ASSIGNED_SCAN_CODE, 0)
            .apply()
    }

    fun getBinding(): Binding {
        val keyCode = prefs.getInt(KEY_ASSIGNED_KEY_CODE, KeyEvent.KEYCODE_UNKNOWN)
        val scanCode = prefs.getInt(KEY_ASSIGNED_SCAN_CODE, 0)
        val background = prefs.getBoolean(KEY_HANDLE_IN_BACKGROUND, false)
        return Binding(
            keyCode = if (keyCode > KeyEvent.KEYCODE_UNKNOWN) keyCode else KeyEvent.KEYCODE_UNKNOWN,
            scanCode = scanCode.coerceAtLeast(0),
            handleInBackground = background,
        )
    }

    fun setBinding(binding: Binding) {
        val safeKey = if (binding.keyCode > KeyEvent.KEYCODE_UNKNOWN) binding.keyCode else KeyEvent.KEYCODE_UNKNOWN
        val safeScan = binding.scanCode.coerceAtLeast(0)
        prefs.edit()
            .putInt(KEY_ASSIGNED_KEY_CODE, safeKey)
            .putInt(KEY_ASSIGNED_SCAN_CODE, safeScan)
            .putBoolean(KEY_HANDLE_IN_BACKGROUND, binding.handleInBackground)
            .apply()
    }

    fun clearBinding() {
        prefs.edit()
            .putInt(KEY_ASSIGNED_KEY_CODE, KeyEvent.KEYCODE_UNKNOWN)
            .putInt(KEY_ASSIGNED_SCAN_CODE, 0)
            .putBoolean(KEY_HANDLE_IN_BACKGROUND, false)
            .apply()
    }

    fun matches(event: KeyEvent): Boolean {
        val binding = getBinding()
        if (!binding.isAssigned()) return false
        if (binding.keyCode != KeyEvent.KEYCODE_UNKNOWN && event.keyCode == binding.keyCode) return true
        if (binding.scanCode > 0 && event.scanCode == binding.scanCode) return true
        return false
    }

    companion object {
        private const val PREF_NAME = "ptt_hardware_key_config"
        private const val KEY_ASSIGNED_KEY_CODE = "assigned_key_code"
        private const val KEY_ASSIGNED_SCAN_CODE = "assigned_scan_code"
        private const val KEY_HANDLE_IN_BACKGROUND = "handle_in_background"
    }
}
