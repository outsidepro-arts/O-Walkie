package ru.outsidepro_arts.owalkie.flutter

import android.content.Context
import android.os.Build
import android.telephony.PhoneStateListener
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import android.util.Log
import androidx.core.content.ContextCompat

/**
 * Monitors cellular call state via [TelephonyCallback] (API 31+) or legacy
 * [PhoneStateListener] and notifies when a call begins or ends.
 *
 * Mirrors the telephony observation logic in the native Android client
 * (`WalkieService.registerTelephonyForRelayPause()`).
 */
class PhoneCallObserver(context: Context) {
    private val appContext = context.applicationContext
    private val telephonyManager =
        appContext.getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager

    private var telephonyCallback: TelephonyCallback? = null
    private var legacyListener: PhoneStateListener? = null

    @Volatile
    private var registered = false

    /**
     * Start observing call state changes. Safe to call multiple times.
     * Requires [android.permission.READ_PHONE_STATE].
     */
    fun register() {
        if (registered) return
        val tm = telephonyManager ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val cb = object : TelephonyCallback(), TelephonyCallback.CallStateListener {
                override fun onCallStateChanged(state: Int) {
                    onTelephonyCallStateChanged(state)
                }
            }
            telephonyCallback = cb
            runCatching {
                tm.registerTelephonyCallback(ContextCompat.getMainExecutor(appContext), cb)
                registered = true
            }.onFailure { Log.w(TAG, "Failed to register TelephonyCallback", it) }
        } else {
            @Suppress("DEPRECATION")
            val listener = object : PhoneStateListener() {
                @Suppress("DEPRECATION")
                override fun onCallStateChanged(state: Int, phoneNumber: String?) {
                    onTelephonyCallStateChanged(state)
                }
            }
            legacyListener = listener
            @Suppress("DEPRECATION")
            runCatching {
                tm.listen(listener, PhoneStateListener.LISTEN_CALL_STATE)
                registered = true
            }.onFailure { Log.w(TAG, "Failed to register PhoneStateListener", it) }
        }
    }

    /** Stop observing call state changes. Safe to call multiple times. */
    fun unregister() {
        if (!registered) return
        val tm = telephonyManager
        telephonyCallback?.let { cb ->
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                runCatching { tm?.unregisterTelephonyCallback(cb) }
            }
            telephonyCallback = null
        }
        legacyListener?.let { listener ->
            @Suppress("DEPRECATION")
            runCatching { tm?.listen(listener, PhoneStateListener.LISTEN_NONE) }
            legacyListener = null
        }
        registered = false
    }

    private fun onTelephonyCallStateChanged(state: Int) {
        when (state) {
            TelephonyManager.CALL_STATE_OFFHOOK -> {
                Log.i(TAG, "Call state: OFFHOOK")
                PlatformEvents.emit(PlatformEvents.EVENT_PHONE_CALL_BEGIN)
            }
            TelephonyManager.CALL_STATE_IDLE -> {
                Log.i(TAG, "Call state: IDLE")
                PlatformEvents.emit(PlatformEvents.EVENT_PHONE_CALL_END)
            }
        }
    }

    companion object {
        private const val TAG = "PhoneCallObserver"
    }
}
