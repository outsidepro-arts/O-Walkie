package ru.outsidepro_arts.owalkie.model

import android.app.Activity
import android.content.Context
import android.content.pm.ActivityInfo
import android.os.Build

/**
 * App-wide screen orientation preference. [Mode.FOLLOW_SYSTEM] maps to [ActivityInfo.SCREEN_ORIENTATION_FULL_USER]
 * (or [ActivityInfo.SCREEN_ORIENTATION_USER] on older API), which follows sensor rotation when the user allows
 * auto-rotate in system settings and honors rotation lock when disabled.
 */
class ScreenOrientationStore(context: Context) {
    private val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    fun getMode(): Mode {
        val raw = prefs.getString(KEY_MODE, null) ?: return Mode.FOLLOW_SYSTEM
        return Mode.fromStorage(raw)
    }

    fun setMode(mode: Mode) {
        prefs.edit().putString(KEY_MODE, mode.storageKey).apply()
    }

    enum class Mode(internal val storageKey: String) {
        PORTRAIT("portrait"),
        LANDSCAPE("landscape"),
        FOLLOW_SYSTEM("follow_system"),
        ;

        companion object {
            fun fromStorage(key: String): Mode =
                entries.firstOrNull { it.storageKey == key } ?: FOLLOW_SYSTEM
        }
    }

    companion object {
        private const val PREF_NAME = "screen_orientation_config"
        private const val KEY_MODE = "app_screen_orientation_mode"

        fun applyTo(activity: Activity) {
            applyTo(activity, ScreenOrientationStore(activity).getMode())
        }

        fun applyTo(activity: Activity, mode: Mode) {
            activity.requestedOrientation = when (mode) {
                Mode.PORTRAIT -> ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                Mode.LANDSCAPE -> ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
                Mode.FOLLOW_SYSTEM ->
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR2) {
                        ActivityInfo.SCREEN_ORIENTATION_FULL_USER
                    } else {
                        @Suppress("DEPRECATION")
                        ActivityInfo.SCREEN_ORIENTATION_USER
                    }
            }
        }
    }
}
