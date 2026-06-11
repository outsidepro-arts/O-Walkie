package ru.outsidepro_arts.owalkie.flutter

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.telephony.TelephonyManager
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * Mirrors [ru.outsidepro_arts.owalkie.WalkieService] network bind + signal reporting:
 * bindProcessToNetwork on validated internet, emit punch/signal events to Flutter.
 */
class SessionNetworkController(context: Context) {
    private val appContext = context.applicationContext
    private val connectivityManager =
        appContext.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private val handler = Handler(Looper.getMainLooper())
    private var callbackRegistered = false
    private var activeNetwork: Network? = null
    private val lastNetworkValidated = AtomicBoolean(false)
    private val lastHandoffNetworkHandle = AtomicLong(Long.MIN_VALUE)
    private var lastSignalRefreshAtMs = 0L
    private var validatedLostRunnable: Runnable? = null

    private val networkCallback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) {
            activeNetwork = network
            updateNetworkReachable("onAvailable")
        }

        override fun onLost(network: Network) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                val fallback = connectivityManager.activeNetwork
                if (fallback != null && fallback != network) {
                    activeNetwork = fallback
                    updateNetworkReachable("onLost/fallback")
                    return
                }
                activeNetwork = null
            } else {
                activeNetwork = null
            }
            if (lastNetworkValidated.getAndSet(false)) {
                PlatformEvents.emit(PlatformEvents.EVENT_NETWORK_LOST)
            }
        }

        override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) {
            activeNetwork = network
            updateNetworkReachable("onCapabilitiesChanged")
        }
    }

    private val signalRunnable = object : Runnable {
        override fun run() {
            if (!running) {
                return
            }
            refreshSignalsIfDue()
            handler.postDelayed(this, SIGNAL_POLL_INTERVAL_MS)
        }
    }

    @Volatile
    private var running = false

    fun start() {
        if (running) {
            return
        }
        running = true
        seedHandoffHandleFromActiveNetwork()
        registerNetworkCallback()
        handler.post(signalRunnable)
    }

    fun stop() {
        if (!running) {
            return
        }
        running = false
        handler.removeCallbacks(signalRunnable)
        validatedLostRunnable?.let { handler.removeCallbacks(it) }
        validatedLostRunnable = null
        unregisterNetworkCallback()
        lastNetworkValidated.set(false)
        lastHandoffNetworkHandle.set(Long.MIN_VALUE)
        activeNetwork = null
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            runCatching { connectivityManager.bindProcessToNetwork(null) }
        }
    }

    private fun registerNetworkCallback() {
        if (callbackRegistered) {
            return
        }
        val request = NetworkRequest.Builder()
            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()
        runCatching {
            connectivityManager.registerNetworkCallback(request, networkCallback)
            callbackRegistered = true
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                val n = connectivityManager.activeNetwork
                if (n != null) {
                    activeNetwork = n
                }
            }
            updateNetworkReachable("registerNetworkCallback")
        }
    }

    private fun unregisterNetworkCallback() {
        if (!callbackRegistered) {
            return
        }
        runCatching { connectivityManager.unregisterNetworkCallback(networkCallback) }
        callbackRegistered = false
    }

    private fun networkCapabilitiesValidated(caps: NetworkCapabilities): Boolean {
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
            caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) &&
            caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_SUSPENDED)
    }

    private fun updateNetworkReachable(reason: String) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            if (!lastNetworkValidated.getAndSet(true)) {
                onNetworkValidated(reason)
            }
            return
        }
        val network = activeNetwork ?: connectivityManager.activeNetwork
        if (network == null) {
            if (lastNetworkValidated.getAndSet(false)) {
                PlatformEvents.emit(PlatformEvents.EVENT_NETWORK_LOST)
            }
            return
        }
        val caps = connectivityManager.getNetworkCapabilities(network) ?: run {
            if (lastNetworkValidated.getAndSet(false)) {
                PlatformEvents.emit(PlatformEvents.EVENT_NETWORK_LOST)
            }
            return
        }
        val validated = networkCapabilitiesValidated(caps)
        if (validated) {
            validatedLostRunnable?.let { handler.removeCallbacks(it) }
            validatedLostRunnable = null
            if (lastNetworkValidated.getAndSet(true)) {
                return
            }
            onNetworkValidated(reason)
            return
        }
        if (!lastNetworkValidated.get()) {
            return
        }
        scheduleValidatedLost(reason)
    }

    private fun scheduleValidatedLost(reason: String) {
        validatedLostRunnable?.let { handler.removeCallbacks(it) }
        val runnable = Runnable {
            validatedLostRunnable = null
            if (lastNetworkValidated.getAndSet(false)) {
                Log.i(TAG, "networkValidated=false ($reason/debounced)")
                PlatformEvents.emit(PlatformEvents.EVENT_NETWORK_LOST)
            }
        }
        validatedLostRunnable = runnable
        handler.postDelayed(runnable, VALIDATED_LOST_DEBOUNCE_MS)
    }

    private fun seedHandoffHandleFromActiveNetwork() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return
        }
        val network = connectivityManager.activeNetwork ?: return
        lastHandoffNetworkHandle.set(network.networkHandle)
        Log.i(TAG, "seedHandoffHandle handle=${network.networkHandle}")
    }

    private fun onNetworkValidated(reason: String) {
        val handle = bindProcessToActiveNetwork()
        val previous = lastHandoffNetworkHandle.get()
        if (handle == previous && previous != Long.MIN_VALUE) {
            Log.i(TAG, "networkValidated skipped same handle=$handle ($reason)")
            refreshSignalsIfDue(force = true)
            return
        }
        lastHandoffNetworkHandle.set(handle)
        Log.i(TAG, "networkHandoff handle=$handle reason=$reason")
        PlatformEvents.emit("${PlatformEvents.EVENT_NETWORK_VALIDATED}:$handle")
        refreshSignalsIfDue(force = true)
    }

    private fun bindProcessToActiveNetwork(): Long {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return 0L
        }
        val network = activeNetwork ?: connectivityManager.activeNetwork
        runCatching { connectivityManager.bindProcessToNetwork(network) }
        return network?.networkHandle ?: 0L
    }

    private fun refreshSignalsIfDue(force: Boolean = false) {
        val now = System.currentTimeMillis()
        if (!force && lastSignalRefreshAtMs != 0L &&
            (now - lastSignalRefreshAtMs) < SIGNAL_REFRESH_INTERVAL_MS
        ) {
            return
        }
        val wifiManager =
            appContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        if (wifiManager != null && wifiManager.isWifiEnabled) {
            @Suppress("DEPRECATION")
            val rssi = wifiManager.connectionInfo?.rssi ?: -100
            PlatformEvents.emitSignalReport(SIGNAL_WIFI, rssi)
        } else {
            PlatformEvents.emitSignalClear(SIGNAL_WIFI)
        }
        val telephonyManager =
            appContext.getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager
        val cellLevel = runCatching { telephonyManager?.signalStrength?.level }.getOrNull()
        if (cellLevel != null) {
            PlatformEvents.emitSignalReport(SIGNAL_CELL, cellLevel)
        } else {
            PlatformEvents.emitSignalClear(SIGNAL_CELL)
        }
        lastSignalRefreshAtMs = now
    }

    companion object {
        private const val TAG = "OwalkieFlutterNet"
        private const val SIGNAL_WIFI = 0
        private const val SIGNAL_CELL = 1
        private const val SIGNAL_POLL_INTERVAL_MS = 5000L
        private const val SIGNAL_REFRESH_INTERVAL_MS = 50_000L
        private const val VALIDATED_LOST_DEBOUNCE_MS = 1500L
    }
}
