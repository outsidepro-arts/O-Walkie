package ru.outsidepro_arts.owalkie.flutter

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.PowerManager
import android.provider.Settings
import android.view.KeyEvent
import android.view.Window
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private var pendingMicResult: MethodChannel.Result? = null
    private var pendingNotificationResult: MethodChannel.Result? = null
    private var sessionNetworkController: SessionNetworkController? = null
    private var capturingHardwarePttKey = false
    private val hardwareKeyStore by lazy { PttHardwareKeyStore(this) }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        EventChannel(
            flutterEngine.dartExecutor.binaryMessenger,
            EVENTS_CHANNEL,
        ).setStreamHandler(
            object : EventChannel.StreamHandler {
                override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
                    PlatformEvents.attach(events)
                }

                override fun onCancel(arguments: Any?) {
                    PlatformEvents.detach()
                }
            },
        )
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "hasMicrophonePermission" -> result.success(hasMicPermission())
                "requestMicrophonePermission" -> requestMicPermission(result)
                "hasNotificationPermission" -> result.success(hasNotificationPermission())
                "requestNotificationPermission" -> requestNotificationPermission(result)
                "prepareAudioSession" -> {
                    val bluetooth = call.argument<Boolean>("bluetoothHeadset") ?: false
                    AudioRouteHelper.applyVoiceAudioProfile(this, bluetooth)
                    result.success(true)
                }
                "releaseAudioSession" -> {
                    AudioRouteHelper.restoreMediaAudioProfile(this)
                    result.success(null)
                }
                "syncPttMediaSession" -> {
                    val active = call.argument<Boolean>("active") ?: false
                    WalkieForegroundService.syncPttMediaSession(this, active)
                    result.success(true)
                }
                "getHardwarePttBinding" -> {
                    val binding = hardwareKeyStore.getBinding()
                    result.success(
                        mapOf(
                            "keyCode" to binding.keyCode,
                            "scanCode" to binding.scanCode,
                            "assigned" to binding.isAssigned(),
                        ),
                    )
                }
                "clearHardwarePttBinding" -> {
                    hardwareKeyStore.clearBinding()
                    result.success(true)
                }
                "startCaptureHardwarePttKey" -> {
                    capturingHardwarePttKey = true
                    result.success(true)
                }
                "cancelCaptureHardwarePttKey" -> {
                    capturingHardwarePttKey = false
                    result.success(true)
                }
                "getExternalControlEnabled" -> {
                    result.success(ExternalControlStore(this).isEnabled())
                }
                "setExternalControlEnabled" -> {
                    val enabled = call.argument<Boolean>("enabled") ?: false
                    ExternalControlStore(this).setEnabled(enabled)
                    result.success(true)
                }
                "startSessionForeground" -> {
                    val connected = call.argument<Boolean>("connected") ?: false
                    WalkieForegroundService.start(this, connected)
                    result.success(true)
                }
                "updateSessionForeground" -> {
                    val connected = call.argument<Boolean>("connected") ?: false
                    WalkieForegroundService.update(this, connected)
                    result.success(true)
                }
                "stopSessionForeground" -> {
                    WalkieForegroundService.stop(this)
                    result.success(true)
                }
                "startSessionNetworkMonitoring" -> {
                    sessionNetworkController()?.start()
                    result.success(true)
                }
                "stopSessionNetworkMonitoring" -> {
                    sessionNetworkController()?.stop()
                    result.success(true)
                }
                "openBatterySettings" -> {
                    openBatteryOptimizationSettings()
                    result.success(true)
                }
                else -> result.notImplemented()
            }
        }
    }

    override fun onCreate(savedInstanceState: android.os.Bundle?) {
        super.onCreate(savedInstanceState)
        installHardwarePttWindowCallback()
        handleBatterySettingsIntent(intent)
        handleMediaButtonIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleBatterySettingsIntent(intent)
        handleMediaButtonIntent(intent)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (interceptHardwarePttKey(event)) {
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        when (requestCode) {
            MIC_REQUEST -> {
                val granted =
                    grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED
                pendingMicResult?.success(granted)
                pendingMicResult = null
            }
            NOTIFICATION_REQUEST -> {
                val granted =
                    grantResults.isNotEmpty() && grantResults[0] == PackageManager.PERMISSION_GRANTED
                pendingNotificationResult?.success(granted)
                pendingNotificationResult = null
            }
        }
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
    }

    /**
     * FlutterView consumes many key events before Activity.dispatchKeyEvent; intercept at the
     * window layer (same keys still reach us during hardware-key capture in settings).
     */
    private fun installHardwarePttWindowCallback() {
        val original = window.callback
        window.callback = object : Window.Callback by original {
            override fun dispatchKeyEvent(event: KeyEvent): Boolean {
                if (interceptHardwarePttKey(event)) {
                    return true
                }
                return original.dispatchKeyEvent(event)
            }
        }
    }

    private fun interceptHardwarePttKey(event: KeyEvent): Boolean {
        if (capturingHardwarePttKey) {
            if (HardwarePttKeyHandler.tryCaptureBinding(this, event) {
                    capturingHardwarePttKey = false
                    PlatformEvents.emit(PlatformEvents.EVENT_HARDWARE_PTT_BOUND)
                }
            ) {
                return true
            }
        }
        return HardwarePttKeyHandler.tryHandlePtt(this, event)
    }

    private fun handleMediaButtonIntent(intent: Intent?) {
        if (intent?.action != Intent.ACTION_MEDIA_BUTTON) {
            return
        }
        val serviceIntent = Intent(intent).apply {
            setClass(this@MainActivity, WalkieForegroundService::class.java)
        }
        startService(serviceIntent)
    }

    private fun handleBatterySettingsIntent(intent: Intent?) {
        if (intent?.action == ACTION_OPEN_BATTERY_SETTINGS) {
            openBatteryOptimizationSettings()
        }
    }

    private fun hasMicPermission(): Boolean {
        return ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.RECORD_AUDIO,
        ) == PackageManager.PERMISSION_GRANTED
    }

    private fun requestMicPermission(result: MethodChannel.Result) {
        if (hasMicPermission()) {
            result.success(true)
            return
        }
        if (pendingMicResult != null) {
            result.error("busy", "Microphone permission request already in progress", null)
            return
        }
        pendingMicResult = result
        ActivityCompat.requestPermissions(
            this,
            arrayOf(Manifest.permission.RECORD_AUDIO),
            MIC_REQUEST,
        )
    }

    private fun hasNotificationPermission(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return true
        }
        return ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.POST_NOTIFICATIONS,
        ) == PackageManager.PERMISSION_GRANTED
    }

    private fun requestNotificationPermission(result: MethodChannel.Result) {
        if (hasNotificationPermission()) {
            result.success(true)
            return
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            result.success(true)
            return
        }
        if (pendingNotificationResult != null) {
            result.error("busy", "Notification permission request already in progress", null)
            return
        }
        pendingNotificationResult = result
        ActivityCompat.requestPermissions(
            this,
            arrayOf(Manifest.permission.POST_NOTIFICATIONS),
            NOTIFICATION_REQUEST,
        )
    }

    private fun sessionNetworkController(): SessionNetworkController {
        val existing = sessionNetworkController
        if (existing != null) {
            return existing
        }
        val created = SessionNetworkController(this)
        sessionNetworkController = created
        return created
    }

    private fun openBatteryOptimizationSettings() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return
        val pm = getSystemService(POWER_SERVICE) as? PowerManager
        if (pm != null && !pm.isIgnoringBatteryOptimizations(packageName)) {
            val direct = Intent(
                Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS,
                Uri.parse("package:$packageName"),
            )
            if (runCatching { startActivity(direct) }.isSuccess) {
                return
            }
        }
        runCatching {
            startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
        }
    }

    companion object {
        const val ACTION_OPEN_BATTERY_SETTINGS =
            "ru.outsidepro_arts.owalkie.flutter.action.OPEN_BATTERY_SETTINGS"
        private const val CHANNEL = "ru.outsidepro_arts.owalkie.flutter/platform"
        private const val EVENTS_CHANNEL = "ru.outsidepro_arts.owalkie.flutter/platform_events"
        private const val MIC_REQUEST = 1001
        private const val NOTIFICATION_REQUEST = 1002
    }
}
