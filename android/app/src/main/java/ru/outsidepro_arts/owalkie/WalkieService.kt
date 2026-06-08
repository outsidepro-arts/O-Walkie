package ru.outsidepro_arts.owalkie

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.AudioDeviceInfo
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.AutomaticGainControl
import android.media.audiofx.NoiseSuppressor
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.os.Binder
import android.os.Build
import android.util.Log
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.telephony.PhoneStateListener
import android.telephony.TelephonyCallback
import android.telephony.TelephonyManager
import android.view.KeyEvent
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import ru.outsidepro_arts.owalkie.model.CallingPatternStore
import ru.outsidepro_arts.owalkie.model.expandedCallingPoints
import ru.outsidepro_arts.owalkie.model.BluetoothHeadsetRouteStore
import ru.outsidepro_arts.owalkie.model.ExternalControlStore
import ru.outsidepro_arts.owalkie.model.MicrophoneConfigStore
import ru.outsidepro_arts.owalkie.model.PhoneCallRelayPauseStore
import ru.outsidepro_arts.owalkie.model.PttHardwareKeyStore
import ru.outsidepro_arts.owalkie.model.RogerPatternStore
import ru.outsidepro_arts.owalkie.model.RxVolumeStore
import ru.outsidepro_arts.owalkie.model.ServerProfile
import ru.outsidepro_arts.owalkie.model.ServerStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.cancelChildren
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import kotlin.math.PI
import kotlin.math.sin

class WalkieService : Service(), NativeRelayBridge.Host {
    companion object {
        private const val TAG = "OwalkieRelay"

        const val ACTION_START = "ru.outsidepro_arts.owalkie.action.START"
        /** Reconnect to another server profile while keeping [desiredConnection]. */
        const val ACTION_SWITCH_SERVER = "ru.outsidepro_arts.owalkie.action.SWITCH_SERVER"
        const val ACTION_CANCEL_CONNECT = "ru.outsidepro_arts.owalkie.action.CANCEL_CONNECT"
        const val ACTION_DISCONNECT_AND_STOP = "ru.outsidepro_arts.owalkie.action.DISCONNECT_AND_STOP"
        const val ACTION_PTT_PRESS = "ru.outsidepro_arts.owalkie.action.PTT_PRESS"
        const val ACTION_PTT_RELEASE = "ru.outsidepro_arts.owalkie.action.PTT_RELEASE"
        const val ACTION_SET_REPEATER = "ru.outsidepro_arts.owalkie.action.SET_REPEATER"
        const val ACTION_CALL_SIGNAL = "ru.outsidepro_arts.owalkie.action.CALL_SIGNAL"
        const val ACTION_EXIT_APP = "ru.outsidepro_arts.owalkie.action.EXIT_APP"
        const val ACTION_TOGGLE_CONNECTION = "ru.outsidepro_arts.owalkie.action.TOGGLE_CONNECTION"
        const val ACTION_STATUS = "ru.outsidepro_arts.owalkie.action.STATUS"
        const val ACTION_SET_RX_VOLUME = "ru.outsidepro_arts.owalkie.action.SET_RX_VOLUME"
        const val ACTION_SET_BLUETOOTH_HEADSET_MODE = "ru.outsidepro_arts.owalkie.action.SET_BLUETOOTH_HEADSET_MODE"
        const val ACTION_SET_ACTIVITY_FOCUS = "ru.outsidepro_arts.owalkie.action.SET_ACTIVITY_FOCUS"
        const val ACTION_SYNC_RELAY_STATUS = "ru.outsidepro_arts.owalkie.action.SYNC_RELAY_STATUS"
        const val ACTION_SYNC_PTT_MEDIA_SESSION = "ru.outsidepro_arts.owalkie.action.SYNC_PTT_MEDIA_SESSION"
        const val ACTION_HARDWARE_PTT_KEY = "ru.outsidepro_arts.owalkie.action.HARDWARE_PTT_KEY"
        const val ACTION_EXTERNAL_PTT_DOWN = ExternalControlReceiver.ACTION_PTT_DOWN
        const val ACTION_EXTERNAL_PTT_UP = ExternalControlReceiver.ACTION_PTT_UP
        const val ACTION_EXTERNAL_PTT_TOGGLE = ExternalControlReceiver.ACTION_PTT_TOGGLE
        const val ACTION_EXTERNAL_CALL_SIGNAL = ExternalControlReceiver.ACTION_CALL_SIGNAL
        const val ACTION_EXTERNAL_CONNECT = ExternalControlReceiver.ACTION_CONNECT
        const val ACTION_EXTERNAL_DISCONNECT = ExternalControlReceiver.ACTION_DISCONNECT
        const val ACTION_EXTERNAL_NEXT_CONNECTION = ExternalControlReceiver.ACTION_NEXT_CONNECTION
        const val ACTION_EXTERNAL_PREVIOUS_CONNECTION = ExternalControlReceiver.ACTION_PREVIOUS_CONNECTION
        const val EXTRA_SIGNAL = "signal"
        const val EXTRA_WS_CONNECTED = "wsConnected"
        const val EXTRA_WS_CONNECTING = "wsConnecting"
        const val EXTRA_UDP_READY = "udpReady"
        const val EXTRA_PROTOCOL_ERROR = "protocolError"
        const val EXTRA_BUSY_MODE = "busyMode"
        const val EXTRA_PTT_SERVER_LOCKED = "pttServerLocked"
        /** Decorative only; unlock is strictly via server `ptt_unlock`. */
        const val EXTRA_PTT_LOCK_DISPLAY_SEC = "pttLockDisplaySec"
        const val EXTRA_RX_ACTIVE = "rxActive"
        const val EXTRA_TX_ACTIVE = "txActive"
        /** Inbound audio while local TX/Roger/Call is active (another station doubling). */
        const val EXTRA_PARALLEL_TX_COLLISION = "parallelTxCollision"
        const val EXTRA_CALL_ACTIVE = "callActive"
        const val EXTRA_PTT_BURST_PRESS_BLOCKED = "pttBurstPressBlocked"
        const val EXTRA_DEBUG_UDP_RECOVERY_COUNT = "debugUdpRecoveryCount"
        const val EXTRA_DEBUG_LAST_UDP_GAP_MS = "debugLastUdpGapMs"
        const val EXTRA_SERVER_HOST = "serverHost"
        /** Single relay port for WebSocket and UDP (same as server `port`). */
        const val EXTRA_SERVER_PORT = "serverPort"
        /** @deprecated Legacy intents; prefer [EXTRA_SERVER_PORT]. */
        const val EXTRA_WS_PORT = "wsPort"
        /** @deprecated Legacy intents; prefer [EXTRA_SERVER_PORT]. */
        const val EXTRA_UDP_PORT = "udpPort"
        const val EXTRA_CHANNEL = "channel"
        const val EXTRA_REPEATER_ENABLED = "repeaterEnabled"
        const val EXTRA_RX_VOLUME_PERCENT = "rxVolumePercent"
        const val EXTRA_USE_BLUETOOTH_HEADSET = "useBluetoothHeadset"
        const val EXTRA_ACTIVITY_FOCUSED = "activityFocused"
        /** Relay transport paused while the system reports an active cellular call ([TelephonyManager.CALL_STATE_OFFHOOK]). */
        const val EXTRA_RELAY_PAUSED_PHONE_CALL = "relayPausedPhoneCall"
        const val EXTRA_HW_KEY_ACTION = "hwKeyAction"
        const val EXTRA_HW_KEY_REPEAT = "hwKeyRepeat"
        const val EXTRA_HW_KEY_CODE = "hwKeyCode"
        const val EXTRA_HW_SCAN_CODE = "hwScanCode"

        private const val NOTIFICATION_CHANNEL_ID = "owalkie_stream"
        private const val NOTIFICATION_ID = 101

        private const val DEFAULT_WS_HOST = ""
        private const val DEFAULT_SERVER_PORT = 5500
        private const val DEFAULT_CHANNEL = "global"

        private const val DEFAULT_SAMPLE_RATE = 8000
        private const val LOCAL_PLAYBACK_SAMPLE_RATE = 44100
        private const val DEFAULT_PACKET_MS = 20
        private const val PROTOCOL_VERSION = 2
        private const val ROGER_TAIL_MS = 40
        /** Quiet period before burst counter / block clears; refreshed on each release and while blocked on each press attempt. */
        private const val PTT_RELEASE_BURST_TIMER_MS = 1000L
        private const val PTT_RELEASE_BURST_BLOCK_THRESHOLD = 3
        private const val CALL_LOCAL_GAIN_DB = -10.0
        private const val NATIVE_RELAY_SHUTDOWN_WAIT_MS = 3000
        /** Full process exit: wait for all native sessions and teardown workers. */
        private const val APP_EXIT_SHUTDOWN_WAIT_MS = 8000

        private val exitRequested = AtomicBoolean(false)
        private val isAppExiting = AtomicBoolean(false)
        private val fullShutdownStarted = AtomicBoolean(false)

        /** Call from UI before [ACTION_EXIT_APP] so [onTaskRemoved] does not restart the service. */
        @JvmStatic
        fun markExitRequested() {
            exitRequested.set(true)
        }
        private const val SIGNAL_POLL_INTERVAL_FOREGROUND_MS = 1000L
        private const val SIGNAL_POLL_INTERVAL_BACKGROUND_MS = 5000L
        private const val SIGNAL_REFRESH_INTERVAL_FOREGROUND_MS = 1000L
        private const val SIGNAL_REFRESH_INTERVAL_BACKGROUND_MS = 50000L

        /** Gap between RX [AudioTrack] writes to treat as a new burst (media session anchor). */
        private const val RX_PLAYBACK_BURST_GAP_MS = 600L
        private const val MEDIA_SESSION_RX_ANCHOR_MIN_INTERVAL_NS = 2_000_000_000L
        /** End parallel-TX collision feedback this long after the last inbound frame while local TX is on. */
        private const val PARALLEL_TX_RX_STALE_NS = 250_000_000L
        private const val TRANSMIT_TIMEOUT_COUNTDOWN_PULSE_MS = 35L
        private const val CLIENT_RECONNECT_INTERVAL_INITIAL_MS = 1500L
        private const val CLIENT_RECONNECT_INTERVAL_MAX_MS = 8000L
        private const val CLIENT_RECONNECT_TIMEOUT_MS = 3500
        private const val CLIENT_INITIAL_CONNECT_TIMEOUT_MS = 5000
        /** Ignore spurious CONNECTION_LOST while UDP/WS settles after a successful connect. */
        private const val CLIENT_RECONNECT_SETTLE_MS = 2500L
    }

    private val binder = Binder()
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private var signalMonitorJob: Job? = null
    private var clientReconnectJob: Job? = null
    private val nativeConnectInFlight = AtomicBoolean(false)
    private val lastRelayConnectedAtMs = AtomicLong(0L)
    private val reconnectSettleUntilMs = AtomicLong(0L)
    private val desiredConnection = AtomicBoolean(false)
    private val relayPausedForCellularCall = AtomicBoolean(false)
    private val configLock = Any()
    private val connectivityManager by lazy {
        getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    }
    private var networkCallbackRegistered = false
    @Volatile
    private var activeCellularOrWifi: Network? = null
    private val pendingUdpNetworkRecreate = AtomicBoolean(false)
    private var lastWelcomeSessionId: Long = 0L
    private val lastNetworkValidated = AtomicBoolean(false)
    private val networkCallback = object : ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network: Network) {
            activeCellularOrWifi = network
            network.hashCode().toLong().let { lastNetworkFingerprint.getAndSet(it) }
            updateNetworkReachableFromCapabilities("onAvailable")
        }

        override fun onLost(network: Network) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                val fallback = connectivityManager.activeNetwork
                if (fallback != null && fallback != network) {
                    activeCellularOrWifi = fallback
                    updateNetworkReachableFromCapabilities("onLost/fallback")
                    return
                } else {
                    activeCellularOrWifi = null
                }
            }
            val fingerprint = network.hashCode().toLong()
            val previous = lastNetworkFingerprint.get()
            if (previous == fingerprint || previous == 0L) {
                lastNetworkFingerprint.set(0L)
                lastNetworkValidated.set(false)
            }
        }

        override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) {
            activeCellularOrWifi = network
            updateNetworkReachableFromCapabilities("onCapabilitiesChanged")
        }
    }

    private var txJob: Job? = null
    private var rogerJob: Job? = null
    private var callSignalJob: Job? = null
    private var localRogerPlaybackJob: Job? = null
    private var localCallPlaybackJob: Job? = null
    private var localPttPressPlaybackJob: Job? = null

    private val transmitting = AtomicBoolean(false)
    private val txLoopRunning = AtomicBoolean(false)
    private val rogerStreaming = AtomicBoolean(false)
    private val callStreaming = AtomicBoolean(false)
    private val sessionId = AtomicLong(0L)
    private val serverRxBroadcastActive = AtomicBoolean(false)
    private val serverPttLocked = AtomicBoolean(false)
    private val pttLockDisplaySec = AtomicInteger(0)
    private val lastInboundUdpAtNs = AtomicLong(0L)
    /** Last UDP audio (not keepalive) received while local TX/Roger/Call is active. */
    private val lastRxDuringParallelTxNs = AtomicLong(0L)
    private val parallelTxCollisionActive = AtomicBoolean(false)
    private val parallelCollisionVibrationRunning = AtomicBoolean(false)

    private val pttReleaseBurstCount = AtomicInteger(0)
    private val pttReleaseBurstPressBlocked = AtomicBoolean(false)
    private var pttReleaseBurstResetJob: Job? = null
    private val udpRecoveryCount = AtomicLong(0L)
    private val lastObservedUdpGapMs = AtomicLong(0L)
    private val lastNetworkFingerprint = AtomicLong(0L)
    private val lastSignalRefreshAtNs = AtomicLong(0L)
    private val lastRxPlaybackWriteNs = AtomicLong(0L)
    /** Last time decoded RX PCM was written to the stream [AudioTrack] (UI "receiving" indicator). */
    private val lastRxAudiblePlaybackWriteNs = AtomicLong(0L)
    private val lastRxMediaSessionAnchorNs = AtomicLong(0L)

    @Volatile
    private var serverHost: String = DEFAULT_WS_HOST

    @Volatile
    private var serverPort: Int = DEFAULT_SERVER_PORT

    @Volatile
    private var channel: String = DEFAULT_CHANNEL

    @Volatile
    private var repeaterEnabled: Boolean = false

    @Volatile
    private var packetMs: Int = DEFAULT_PACKET_MS
    @Volatile
    private var serverSampleRate: Int = DEFAULT_SAMPLE_RATE
    @Volatile
    private var wsSecure: Boolean = false
    @Volatile
    private var channelBound: Boolean = false
    @Volatile
    private var protocolError: Boolean = false
    @Volatile
    private var busyMode: Boolean = false

    private var txCountdownJob: Job? = null

    private lateinit var audioManager: AudioManager
    private lateinit var rogerPatternStore: RogerPatternStore
    private lateinit var callingPatternStore: CallingPatternStore
    private lateinit var microphoneConfigStore: MicrophoneConfigStore
    private lateinit var bluetoothHeadsetRouteStore: BluetoothHeadsetRouteStore
    private lateinit var rxVolumeStore: RxVolumeStore
    private lateinit var pttHardwareKeyStore: PttHardwareKeyStore
    private lateinit var externalControlStore: ExternalControlStore
    private lateinit var phoneCallRelayPauseStore: PhoneCallRelayPauseStore
    private var telephonyCallStateCallback: TelephonyCallback? = null
    @Suppress("DEPRECATION")
    private var legacyPhoneStateListener: PhoneStateListener? = null
    private var localPttPressPcm: ShortArray = shortArrayOf()
    private var localPttReleasePcm: ShortArray = shortArrayOf()
    @Volatile
    private var voiceProfileActive: Boolean = false
    @Volatile
    private var rxVolumePercent: Int = RxVolumeStore.DEFAULT_RX_VOLUME_PERCENT
    @Volatile
    private var useBluetoothHeadset: Boolean = false
    @Volatile
    private var activityFocused: Boolean = true

    private val mainHandler = Handler(Looper.getMainLooper())
    private var pttLockDisplayTickRunnable: Runnable? = null
    private lateinit var mediaButtonPttToggleRunnable: Runnable
    private var pttMediaSessionController: PttMediaSessionController? = null

    private val nativeRelayLazy = lazy { NativeRelayBridge(this) }
    private lateinit var rxJitterBuffer: RxPcmJitterBuffer

    private fun relay(): NativeRelayBridge = nativeRelayLazy.value

    private fun relayTransportConnected(): Boolean {
        val bridge = relay()
        if (bridge.isConnected) {
            if (!bridge.isNativeSessionReady()) {
                bridge.syncTransportStateFromNative()
            }
            return bridge.isConnected
        }
        if (bridge.isNativeSessionReady()) {
            bridge.syncTransportStateFromNative()
            bridge.resyncReadyFromNative()
            return bridge.isConnected || bridge.isNativeSessionReady()
        }
        bridge.syncTransportStateFromNative()
        return bridge.isConnected
    }

    private fun relayTransportConnecting(): Boolean {
        if (relayTransportConnected()) {
            return false
        }
        return relay().isConnecting ||
            (desiredConnection.get() && !relayPausedForCellularCall.get())
    }

    @Volatile
    private var nativeDisconnectJob: Job? = null

    private suspend fun waitForNativeConnectIdle(maxWaitMs: Long = 12_000L): Boolean {
        var waitedMs = 0L
        while (nativeConnectInFlight.get() && waitedMs < maxWaitMs) {
            delay(50)
            waitedMs += 50
        }
        return !nativeConnectInFlight.get()
    }

    private suspend fun tearDownNativeRelay(sessionId: Long) {
        OwalkieNative.ensureLoaded()
        waitForNativeConnectIdle()
        if (sessionId != 0L) {
            OwalkieNative.nativeDisconnect(sessionId)
            waitForNativeConnectIdle(2_000L)
            if (OwalkieNative.nativeSessionValid(sessionId) != 0) {
                Log.w(TAG, "tearDownNativeRelay session=$sessionId still valid after disconnect")
                OwalkieNative.nativeDisconnect(sessionId)
                waitForNativeConnectIdle(2_000L)
            }
        } else {
            OwalkieNative.nativeDisconnectAllAndWait(NATIVE_RELAY_SHUTDOWN_WAIT_MS)
        }
    }

    private fun beginNativeTeardown(sessionId: Long): Job {
        nativeDisconnectJob?.cancel()
        val job = serviceScope.launch(Dispatchers.IO) {
            tearDownNativeRelay(sessionId)
        }
        nativeDisconnectJob = job
        return job
    }

    private fun requestNativeRelayDisconnect() {
        val sessionId = relay().detachLocalTransportState()
        beginNativeTeardown(sessionId)
    }

    private suspend fun awaitNativeDisconnectIdle() {
        nativeDisconnectJob?.join()
        nativeDisconnectJob = null
    }

    private fun runOnMain(block: () -> Unit) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            block()
        } else {
            mainHandler.post(block)
        }
    }

    override fun onCreate() {
        super.onCreate()
        exitRequested.set(false)
        isAppExiting.set(false)
        fullShutdownStarted.set(false)
        audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        rogerPatternStore = RogerPatternStore(this)
        callingPatternStore = CallingPatternStore(this)
        microphoneConfigStore = MicrophoneConfigStore(this)
        bluetoothHeadsetRouteStore = BluetoothHeadsetRouteStore(this)
        rxVolumeStore = RxVolumeStore(this)
        pttHardwareKeyStore = PttHardwareKeyStore(this)
        externalControlStore = ExternalControlStore(this)
        phoneCallRelayPauseStore = PhoneCallRelayPauseStore(this)
        mediaButtonPttToggleRunnable = Runnable { handleMediaButtonPttToggle() }
        useBluetoothHeadset = bluetoothHeadsetRouteStore.isEnabled()
        rxVolumePercent = rxVolumeStore.getPercent()
        val pttPressWav = loadWavPcmFromRaw(R.raw.selfpttup_002)
        localPttPressPcm = resampleLinear(pttPressWav.samples, pttPressWav.sampleRate, LOCAL_PLAYBACK_SAMPLE_RATE)
        val pttReleaseWav = loadWavPcmFromRaw(R.raw.selfttdown_002)
        localPttReleasePcm = resampleLinear(pttReleaseWav.samples, pttReleaseWav.sampleRate, LOCAL_PLAYBACK_SAMPLE_RATE)
        rxJitterBuffer = RxPcmJitterBuffer(
            sampleRateProvider = { currentCodecSampleRate() },
            frameSamplesProvider = { currentFrameSamples() },
        ).also { buffer ->
            buffer.setListener { nowNs, gapMsSinceLastWrite ->
                lastRxPlaybackWriteNs.set(nowNs)
                lastRxAudiblePlaybackWriteNs.set(nowNs)
                if (gapMsSinceLastWrite > RX_PLAYBACK_BURST_GAP_MS) {
                    maybeAnchorMediaSessionOnRxPlaybackWrite(nowNs)
                }
            }
        }
        registerNetworkCallback()
        registerTelephonyForRelayPause()
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (isAppExiting.get() || exitRequested.get()) {
            if (intent?.action == ACTION_EXIT_APP) {
                exitApp()
            }
            return START_NOT_STICKY
        }
        if (intent?.action == Intent.ACTION_MEDIA_BUTTON) {
            if (shouldOfferMediaButtonCapture()) {
                syncPttMediaSession()
                pttMediaSessionController?.dispatchMediaButtonIntent(intent)
            }
            return START_STICKY
        }
        when (intent?.action) {
            ACTION_START -> {
                desiredConnection.set(true)
                startCore(intent)
            }
            ACTION_SWITCH_SERVER -> {
                desiredConnection.set(true)
                switchServerProfile(intent)
            }
            ACTION_CANCEL_CONNECT -> cancelConnection()
            ACTION_DISCONNECT_AND_STOP -> disconnectAndStopService()
            ACTION_TOGGLE_CONNECTION -> {
                if (desiredConnection.get() || relayTransportConnected()) {
                    cancelConnection()
                } else {
                    desiredConnection.set(true)
                    startCore(null)
                }
            }
            ACTION_PTT_PRESS -> onPttPress()
            ACTION_PTT_RELEASE -> onPttRelease()
            ACTION_HARDWARE_PTT_KEY -> {
                val action = intent.getIntExtra(EXTRA_HW_KEY_ACTION, KeyEvent.ACTION_DOWN)
                val repeat = intent.getIntExtra(EXTRA_HW_KEY_REPEAT, 0)
                val keyCode = intent.getIntExtra(EXTRA_HW_KEY_CODE, KeyEvent.KEYCODE_UNKNOWN)
                val scanCode = intent.getIntExtra(EXTRA_HW_SCAN_CODE, 0)
                val evt = KeyEvent(0L, 0L, action, keyCode, repeat, 0, 0, scanCode)
                handleHardwarePttKeyEvent(evt)
            }
            ACTION_SET_REPEATER -> setRepeaterMode(intent.getBooleanExtra(EXTRA_REPEATER_ENABLED, false))
            ACTION_CALL_SIGNAL -> onCallSignal()
            ACTION_SET_RX_VOLUME -> updateRxVolumePercent(
                intent.getIntExtra(EXTRA_RX_VOLUME_PERCENT, rxVolumePercent),
            )
            ACTION_SET_BLUETOOTH_HEADSET_MODE -> updateBluetoothHeadsetMode(
                intent.getBooleanExtra(EXTRA_USE_BLUETOOTH_HEADSET, useBluetoothHeadset),
            )
            ACTION_SET_ACTIVITY_FOCUS -> updateActivityFocus(
                intent.getBooleanExtra(EXTRA_ACTIVITY_FOCUSED, activityFocused),
            )
            ACTION_SYNC_RELAY_STATUS -> publishRelayStatus()
            ACTION_SYNC_PTT_MEDIA_SESSION -> syncPttMediaSession()
            ACTION_EXTERNAL_PTT_DOWN -> if (externalControlStore.isEnabled()) onPttPress()
            ACTION_EXTERNAL_PTT_UP -> if (externalControlStore.isEnabled()) onPttRelease()
            ACTION_EXTERNAL_PTT_TOGGLE -> if (externalControlStore.isEnabled()) togglePttFromExternal()
            ACTION_EXTERNAL_CALL_SIGNAL -> if (externalControlStore.isEnabled()) onCallSignal()
            ACTION_EXTERNAL_CONNECT -> if (externalControlStore.isEnabled()) connectFromExternal()
            ACTION_EXTERNAL_DISCONNECT -> if (externalControlStore.isEnabled()) cancelConnection()
            ACTION_EXTERNAL_NEXT_CONNECTION -> if (externalControlStore.isEnabled()) switchConnectionProfileFromExternal(1)
            ACTION_EXTERNAL_PREVIOUS_CONNECTION -> if (externalControlStore.isEnabled()) switchConnectionProfileFromExternal(-1)
            ACTION_EXIT_APP -> exitApp()
        }
        return START_STICKY
    }

    private fun handleHardwarePttKeyEvent(event: KeyEvent): Boolean {
        if (!isRelaySessionReady()) return false
        val binding = pttHardwareKeyStore.getBinding()
        if (!binding.isAssigned()) return false
        if (!pttHardwareKeyStore.matches(event)) return false
        val toggleMode = pttHardwareKeyStore.isToggleModeEnabled()

        if (toggleMode) {
            return when (event.action) {
                KeyEvent.ACTION_DOWN -> {
                    if (event.repeatCount == 0) {
                        if (transmitting.get()) {
                            onPttRelease()
                        } else {
                            onPttPress()
                        }
                    }
                    true
                }
                KeyEvent.ACTION_UP -> true
                else -> false
            }
        }

        return when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                if (event.repeatCount == 0) {
                    onPttPress()
                }
                true
            }
            KeyEvent.ACTION_UP -> {
                onPttRelease()
                true
            }
            else -> false
        }
    }

    override fun onDestroy() {
        pttMediaSessionController?.release()
        pttMediaSessionController = null
        if (!fullShutdownStarted.get()) {
            Thread({
                runBlocking(Dispatchers.IO) {
                    performFullShutdown(logTag = "onDestroy")
                }
            }, "owalkie-onDestroy").start()
        }
        serviceScope.cancel()
        super.onDestroy()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        super.onTaskRemoved(rootIntent)
        if (exitRequested.get() || isAppExiting.get()) return
        if (!desiredConnection.get()) return
        if (relayPausedForCellularCall.get()) return
        val restartIntent = Intent(applicationContext, WalkieService::class.java).apply {
            action = ACTION_START
            putExtra(EXTRA_SERVER_HOST, serverHost)
            putExtra(EXTRA_SERVER_PORT, serverPort)
            putExtra(EXTRA_CHANNEL, channel)
            putExtra(EXTRA_REPEATER_ENABLED, repeaterEnabled)
        }
        runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                applicationContext.startForegroundService(restartIntent)
            } else {
                applicationContext.startService(restartIntent)
            }
        }
    }

    private fun startCore(intent: Intent?) {
        val changed = applyConfigFromIntent(intent)
        enforceAudioRoutePolicy()
        startForegroundInternal()
        connectRelay(force = true)
        ensureSignalMonitorLoop()
        ensureClientReconnectLoop()
    }

    /** Hot-swap server profile without clearing user connect intent (next/prev server buttons). */
    private fun switchServerProfile(intent: Intent?) {
        applyConfigFromIntent(intent)
        enforceAudioRoutePolicy()
        resetRelayCodecState()
        serviceScope.launch(Dispatchers.IO) {
            tearDownTransportAndAwait(clearUserIntent = false)
            withContext(Dispatchers.Main) {
                startForegroundInternal()
                broadcastStatus(currentSignalByte())
            }
            connectNativeRelay(force = true)
            ensureSignalMonitorLoop()
            ensureClientReconnectLoop()
        }
    }

    private fun shouldDeferReconnectAttempt(sessionId: Long): Boolean {
        if (sessionId == 0L) {
            return false
        }
        val elapsed = System.currentTimeMillis() - lastRelayConnectedAtMs.get()
        if (elapsed in 0 until CLIENT_RECONNECT_SETTLE_MS &&
            OwalkieNative.nativeSessionReady(sessionId) != 0
        ) {
            return true
        }
        return false
    }

    private fun markReconnectSettle(durationMs: Long = CLIENT_RECONNECT_SETTLE_MS) {
        reconnectSettleUntilMs.set(System.currentTimeMillis() + durationMs)
    }

    private fun inReconnectSettle(): Boolean {
        val until = reconnectSettleUntilMs.get()
        return until != 0L && System.currentTimeMillis() < until
    }

    private fun clearReconnectSettle() {
        reconnectSettleUntilMs.set(0L)
    }

    private fun stopClientReconnectLoop() {
        clientReconnectJob?.cancel()
        clientReconnectJob = null
    }

    private fun ensureClientReconnectLoop() {
        if (clientReconnectJob?.isActive == true) return
        clientReconnectJob = serviceScope.launch(Dispatchers.IO) {
            var backoffMs = CLIENT_RECONNECT_INTERVAL_INITIAL_MS
            while (isActive) {
                if (!desiredConnection.get() || relayPausedForCellularCall.get()) {
                    delay(500)
                    continue
                }
                if (relayTransportConnected()) {
                    backoffMs = CLIENT_RECONNECT_INTERVAL_INITIAL_MS
                    delay(500)
                    continue
                }
                if (nativeConnectInFlight.get()) {
                    delay(300)
                    continue
                }
                if (!lastNetworkValidated.get()) {
                    delay(500)
                    continue
                }
                bindProcessToActiveNetwork()
                val bridge = relay()
                val sessionId = bridge.activeSessionId
                if (inReconnectSettle()) {
                    bridge.resyncReadyFromNative()
                    delay(300)
                    continue
                }
                val nativeValid =
                    sessionId != 0L && OwalkieNative.nativeSessionValid(sessionId) != 0
                if (nativeValid) {
                    if (bridge.isNativeSessionReady()) {
                        if (bridge.syncTransportStateFromNative() || bridge.resyncReadyFromNative()) {
                            clearReconnectSettle()
                            backoffMs = CLIENT_RECONNECT_INTERVAL_INITIAL_MS
                        }
                        delay(500)
                        continue
                    }
                    Log.i(TAG, "clientConnect session=$sessionId backoff=${backoffMs}ms")
                    tryNativeConnect(CLIENT_RECONNECT_TIMEOUT_MS)
                    if (relayTransportConnected()) {
                        clearReconnectSettle()
                        backoffMs = CLIENT_RECONNECT_INTERVAL_INITIAL_MS
                    } else {
                        markReconnectSettle()
                    }
                } else if (!nativeConnectInFlight.get()) {
                    if (sessionId != 0L) {
                        Log.w(TAG, "clientConnect stale kotlin session=$sessionId; native teardown")
                        OwalkieNative.nativeDisconnect(sessionId)
                        relay().detachLocalTransportState()
                    }
                    connectNativeRelay(force = true)
                }
                delay(backoffMs)
                backoffMs = minOf(
                    (backoffMs * 1.5).toLong().coerceAtLeast(1L),
                    CLIENT_RECONNECT_INTERVAL_MAX_MS,
                )
            }
        }
    }

    private fun resetNativeRelayUiTransportState() {
        channelBound = false
        protocolError = false
        busyMode = false
        clearServerSessionControlState()
    }

    private fun resetRelayCodecState() {
        sessionId.set(0)
        channelBound = false
        releaseRxPlaybackTrack()
    }

    private fun startForegroundInternal() {
        ensureNotificationChannel()
        val isConnectingOrConnected = desiredConnection.get() || relayTransportConnected()
        val actionLabel = if (isConnectingOrConnected) {
            getString(R.string.service_notification_action_disconnect)
        } else {
            getString(R.string.service_notification_action_connect)
        }
        val notification: Notification = NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle(getString(R.string.service_notification_title))
            .setContentText(getString(R.string.service_notification_text))
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setOngoing(true)
            .setContentIntent(mainActivityPendingIntent())
            .addAction(
                0,
                actionLabel,
                toggleConnectionPendingIntent(),
            )
            .addAction(
                0,
                getString(R.string.service_notification_action_battery),
                batterySettingsPendingIntent(),
            )
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
        startForeground(NOTIFICATION_ID, notification)
    }

    private fun updateForegroundNotification() {
        ensureNotificationChannel()
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val isConnectingOrConnected = desiredConnection.get() || relayTransportConnected()
        val actionLabel = if (isConnectingOrConnected) {
            getString(R.string.service_notification_action_disconnect)
        } else {
            getString(R.string.service_notification_action_connect)
        }
        val notification: Notification = NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle(getString(R.string.service_notification_title))
            .setContentText(getString(R.string.service_notification_text))
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setOngoing(true)
            .setContentIntent(mainActivityPendingIntent())
            .addAction(
                0,
                actionLabel,
                toggleConnectionPendingIntent(),
            )
            .addAction(
                0,
                getString(R.string.service_notification_action_battery),
                batterySettingsPendingIntent(),
            )
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
        nm.notify(NOTIFICATION_ID, notification)
    }

    private fun ensureNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        val channel = NotificationChannel(
            NOTIFICATION_CHANNEL_ID,
            getString(R.string.service_channel_name),
            NotificationManager.IMPORTANCE_LOW,
        )
        nm.createNotificationChannel(channel)
    }

    private fun mainActivityPendingIntent(): PendingIntent {
        val intent = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        return PendingIntent.getActivity(this, 2001, intent, flags)
    }

    private fun batterySettingsPendingIntent(): PendingIntent {
        val intent = Intent(this, MainActivity::class.java).apply {
            action = MainActivity.ACTION_OPEN_BATTERY_SETTINGS
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP
        }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        return PendingIntent.getActivity(this, 2002, intent, flags)
    }

    private fun toggleConnectionPendingIntent(): PendingIntent {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = ACTION_TOGGLE_CONNECTION
        }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        return PendingIntent.getService(this, 2003, intent, flags)
    }

    private fun connectRelay(force: Boolean = false) {
        connectNativeRelay(force)
    }

    private fun tryNativeConnect(timeoutMs: Int): Int {
        if (!nativeConnectInFlight.compareAndSet(false, true)) {
            return OwalkieNative.ERR_NOT_READY
        }
        return try {
            val bridge = relay()
            val rc = bridge.activateConnection(timeoutMs)
            if (rc == OwalkieNative.OK) {
                bridge.resyncReadyFromNative()
            }
            rc
        } finally {
            nativeConnectInFlight.set(false)
        }
    }

    private fun connectNativeRelay(force: Boolean = false) {
        if (!desiredConnection.get()) return
        if (relayPausedForCellularCall.get()) return
        if (!force && relay().activeSessionId != 0L) return
        if (!force && (relayTransportConnected() || relayTransportConnecting())) return
        if (!nativeConnectInFlight.compareAndSet(false, true)) return

        serviceScope.launch(Dispatchers.IO) {
            awaitNativeDisconnectIdle()
            var connectOk = false
            var invalidEndpoint = false
            val endpoint = parseServerEndpoint(serverHost, serverPort)
            try {
                if (endpoint == null || endpoint.wsSecure) {
                    invalidEndpoint = true
                } else if (!desiredConnection.get() || relayPausedForCellularCall.get()) {
                    connectOk = false
                } else {
                    bindProcessToActiveNetwork()
                    val bridge = relay()
                    currentSignalByte()
                    applyRelayPowerProfile()
                    val selectedChannel = channel.trim().ifBlank { DEFAULT_CHANNEL }
                    if (bridge.activeSessionId == 0L) {
                        OwalkieNative.nativeDisconnectAll()
                    }
                    connectOk = bridge.prepareConnection(
                        endpoint.host,
                        endpoint.port,
                        selectedChannel,
                        repeaterEnabled,
                        endpoint.wsSecure,
                    )
                    if (connectOk && desiredConnection.get()) {
                        val rc = bridge.activateConnection(CLIENT_INITIAL_CONNECT_TIMEOUT_MS)
                        if (rc == OwalkieNative.OK) {
                            bridge.resyncReadyFromNative()
                        }
                        connectOk = relayTransportConnected()
                        if (!connectOk) {
                            markReconnectSettle()
                        } else {
                            clearReconnectSettle()
                        }
                    }
                    if (!connectOk || !desiredConnection.get()) {
                        bridge.disconnect()
                        connectOk = false
                    }
                }
            } finally {
                nativeConnectInFlight.set(false)
                withContext(Dispatchers.Main) {
                    if (invalidEndpoint) {
                        channelBound = false
                        if (endpoint?.wsSecure == true) {
                            protocolError = true
                            relayPausedForCellularCall.set(false)
                            desiredConnection.set(false)
                        }
                    } else if (!connectOk) {
                        channelBound = false
                    }
                    broadcastStatus(currentSignalByte())
                }
            }
        }
    }

    private fun releaseRxPlaybackTrack() {
        if (::rxJitterBuffer.isInitialized) {
            rxJitterBuffer.release()
        }
    }

    private fun forceStopTransmissionFromServer() {
        stopTxCountdownFromServer()
        if (transmitting.get()) {
            onPttRelease()
            return
        }
        if (rogerStreaming.getAndSet(false)) {
            rogerJob?.cancel()
            rogerJob = null
        }
        if (callStreaming.getAndSet(false)) {
            callSignalJob?.cancel()
            callSignalJob = null
        }
        releaseVoiceProfileIfIdle()
        broadcastStatus(currentSignalByte())
    }

    private fun processInboundRxPcm(pcm: ShortArray, sampleRate: Int, packetMs: Int) {
        if (pcm.isEmpty()) return
        val nowNs = System.nanoTime()
        val previousInboundNs = lastInboundUdpAtNs.getAndSet(nowNs)
        if (previousInboundNs > 0L && nowNs > previousInboundNs) {
            lastObservedUdpGapMs.set((nowNs - previousInboundNs) / 1_000_000L)
        }
        val txActive = transmitting.get() || rogerStreaming.get() || callStreaming.get()
        if (txActive) {
            lastRxDuringParallelTxNs.set(nowNs)
        }
        if (transmitting.get() || rogerStreaming.get() || callStreaming.get()) {
            return
        }
        val scaled = applyRxVolume(pcm)
        if (scaled.isEmpty()) return
        rxJitterBuffer.offer(scaled)
    }

    private fun resetPttReleaseBurstGuard() {
        pttReleaseBurstResetJob?.cancel()
        pttReleaseBurstResetJob = null
        pttReleaseBurstCount.set(0)
        pttReleaseBurstPressBlocked.set(false)
    }

    /** Schedules (or refreshes) decay: after [PTT_RELEASE_BURST_TIMER_MS] quiet, counter and block clear. */
    private fun schedulePttReleaseBurstDecay() {
        pttReleaseBurstResetJob?.cancel()
        pttReleaseBurstResetJob = serviceScope.launch {
            delay(PTT_RELEASE_BURST_TIMER_MS)
            pttReleaseBurstCount.set(0)
            pttReleaseBurstPressBlocked.set(false)
            broadcastStatus(currentSignalByte())
        }
    }

    /**
     * Each completed release increments count; from [PTT_RELEASE_BURST_BLOCK_THRESHOLD] onward presses are blocked.
     * Decay timer restarts on every release; while blocked, rejected presses also restart the timer so spam delays unblock.
     */
    private fun recordPttReleaseBurstGuard() {
        val n = pttReleaseBurstCount.incrementAndGet()
        if (n >= PTT_RELEASE_BURST_BLOCK_THRESHOLD) {
            pttReleaseBurstPressBlocked.set(true)
        }
        schedulePttReleaseBurstDecay()
        broadcastStatus(currentSignalByte())
    }

    private fun systemVibrator(): Vibrator? {
        val v = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            getSystemService(VibratorManager::class.java)?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            getSystemService(VIBRATOR_SERVICE) as? Vibrator
        }
        return if (v != null && v.hasVibrator()) v else null
    }

    private fun startParallelCollisionVibration() {
        runCatching {
            val vibrator = systemVibrator() ?: return
            vibrator.cancel()
            // Short pulse + gap, repeat (index 1 = start of repeating segment).
            val pattern = longArrayOf(0, 26, 74)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator.vibrate(VibrationEffect.createWaveform(pattern, 1))
            } else {
                @Suppress("DEPRECATION")
                vibrator.vibrate(pattern, 1)
            }
        }
    }

    private fun stopParallelCollisionVibration() {
        runCatching { systemVibrator()?.cancel() }
    }

    private fun playTransmitTimeLimitPulse() {
        runCatching {
            val vibrator = systemVibrator() ?: return@runCatching
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator.vibrate(
                    VibrationEffect.createOneShot(
                        TRANSMIT_TIMEOUT_COUNTDOWN_PULSE_MS,
                        VibrationEffect.DEFAULT_AMPLITUDE,
                    ),
                )
            } else {
                @Suppress("DEPRECATION")
                vibrator.vibrate(TRANSMIT_TIMEOUT_COUNTDOWN_PULSE_MS)
            }
        }
    }

    private fun startTxCountdownFromServer() {
        stopTxCountdownFromServer()
        txCountdownJob = serviceScope.launch(Dispatchers.Default) {
            while (isActive) {
                if (!transmitting.get()) {
                    break
                }
                playTransmitTimeLimitPulse()
                delay(1000L)
            }
        }
    }

    private fun stopTxCountdownFromServer() {
        txCountdownJob?.cancel()
        txCountdownJob = null
    }

    private fun syncParallelCollisionVibration(active: Boolean) {
        if (active) {
            if (parallelCollisionVibrationRunning.compareAndSet(false, true)) {
                startParallelCollisionVibration()
            }
        } else {
            if (parallelCollisionVibrationRunning.compareAndSet(true, false)) {
                stopParallelCollisionVibration()
            }
        }
    }

    private fun refreshParallelTxCollisionState() {
        val nowNs = System.nanoTime()
        val txOn = transmitting.get() || rogerStreaming.get() || callStreaming.get()
        val last = lastRxDuringParallelTxNs.get()
        val active = txOn && last != 0L && (nowNs - last) <= PARALLEL_TX_RX_STALE_NS
        val prev = parallelTxCollisionActive.get()
        if (prev != active) {
            parallelTxCollisionActive.set(active)
            syncParallelCollisionVibration(active)
        }
    }

    private fun onPttPress() {
        if (serverPttLocked.get()) return
        rogerJob?.cancel()
        rogerJob = null
        rogerStreaming.set(false)
        callSignalJob?.cancel()
        callSignalJob = null
        callStreaming.set(false)
        localCallPlaybackJob?.cancel()
        localCallPlaybackJob = null
        lastRxDuringParallelTxNs.set(0L)
        stopTxCountdownFromServer()
        if (transmitting.getAndSet(true)) return
        relay().setPowerActiveTx()
        if (!relay().txStart()) {
            transmitting.set(false)
            broadcastStatus(currentSignalByte())
            return
        }
        if (pttReleaseBurstPressBlocked.get()) {
            transmitting.set(false)
            relay().txEnd()
            schedulePttReleaseBurstDecay()
            broadcastStatus(currentSignalByte())
            return
        }
        val micOption = microphoneConfigStore.getSelectedOption()
        ensureVoiceAudioProfile(useBluetoothHeadset || micOption.preferBluetooth)
        localPttPressPlaybackJob?.cancel()
        localPttPressPlaybackJob = serviceScope.launch(Dispatchers.IO) {
            playLocalSignalPcm(localPttPressPcm, LOCAL_PLAYBACK_SAMPLE_RATE, 0.0)
        }
        if (!txLoopRunning.compareAndSet(false, true)) {
            transmitting.set(false)
            relay().txEnd()
            broadcastStatus(currentSignalByte())
            return
        }
        if (txJob?.isActive == true) {
            txLoopRunning.set(false)
            transmitting.set(false)
            relay().txEnd()
            broadcastStatus(currentSignalByte())
            return
        }
        txJob = serviceScope.launch(Dispatchers.Default) {
            try {
                runCaptureLoop()
            } finally {
                txLoopRunning.set(false)
                txJob = null
            }
        }
    }

    private fun onPttRelease() {
        if (!transmitting.getAndSet(false)) return
        stopTxCountdownFromServer()
        recordPttReleaseBurstGuard()
        if (rogerStreaming.getAndSet(true)) return
        localRogerPlaybackJob?.cancel()
        rogerJob = serviceScope.launch(Dispatchers.Default) {
            try {
                val rogerPcm = generateRogerFromSelectedPattern(currentCodecSampleRate())
                val localRogerPcm = generateRogerFromSelectedPattern(LOCAL_PLAYBACK_SAMPLE_RATE)
                val localPlaybackPcm = prependSignal(localPttReleasePcm, localRogerPcm)
                localRogerPlaybackJob = serviceScope.launch(Dispatchers.IO) {
                    playLocalRogerPcm(localPlaybackPcm, LOCAL_PLAYBACK_SAMPLE_RATE)
                }
                streamRogerBeep(rogerPcm)
                relay().txEnd()
            } finally {
                rogerStreaming.set(false)
                releaseVoiceProfileIfIdle()
                drainPendingUdpNetworkRecreate()
            }
        }
    }

    private fun onCallSignal() {
        if (serverPttLocked.get()) return
        if (transmitting.get() || rogerStreaming.get()) return
        if (callStreaming.getAndSet(true)) return
        lastRxDuringParallelTxNs.set(0L)
        ensureVoiceAudioProfile(useBluetoothHeadset)
        localCallPlaybackJob?.cancel()
        callSignalJob = serviceScope.launch(Dispatchers.Default) {
            try {
                val callPcm = generateCallFromSelectedPattern(currentCodecSampleRate())
                val localCallPcm = generateCallFromSelectedPattern(LOCAL_PLAYBACK_SAMPLE_RATE)
                localCallPlaybackJob = serviceScope.launch(Dispatchers.IO) {
                    playLocalSignalPcm(localCallPcm, LOCAL_PLAYBACK_SAMPLE_RATE, CALL_LOCAL_GAIN_DB)
                }
                relay().txStart()
                streamGeneratedSignal(callPcm)
                relay().txEnd()
            } finally {
                callStreaming.set(false)
                releaseVoiceProfileIfIdle()
                drainPendingUdpNetworkRecreate()
            }
        }
    }

    private fun togglePttFromExternal() {
        if (transmitting.get()) {
            onPttRelease()
        } else {
            onPttPress()
        }
    }

    private fun connectFromExternal() {
        val profile = loadSelectedServerProfile() ?: return
        desiredConnection.set(true)
        startCore(
            Intent(this, WalkieService::class.java).apply {
                putExtra(EXTRA_SERVER_HOST, profile.host)
                putExtra(EXTRA_SERVER_PORT, profile.port)
                putExtra(EXTRA_CHANNEL, profile.channel)
                putExtra(EXTRA_REPEATER_ENABLED, repeaterEnabled)
                putExtra(EXTRA_RX_VOLUME_PERCENT, rxVolumePercent)
                putExtra(EXTRA_USE_BLUETOOTH_HEADSET, useBluetoothHeadset)
            },
        )
    }

    private fun switchConnectionProfileFromExternal(step: Int) {
        if (step == 0) return
        val store = ServerStore(this)
        val servers = store.load()
        if (servers.isEmpty()) return
        val selectedName = store.getLastSelectedName()
        val currentIndex = servers.indexOfFirst { it.name == selectedName }
            .takeIf { it >= 0 } ?: 0
        val targetIndex = (currentIndex + step).coerceIn(0, servers.lastIndex)
        if (targetIndex == currentIndex) return
        val selected = servers[targetIndex]
        store.setLastSelectedName(selected.name)
        if (desiredConnection.get() || relayTransportConnected()) {
            switchServerProfileIntent(selected)
        }
    }

    private fun switchServerProfileIntent(profile: ServerProfile) {
        val intent = Intent(this, WalkieService::class.java).apply {
            action = ACTION_SWITCH_SERVER
            putExtra(EXTRA_SERVER_HOST, profile.host)
            putExtra(EXTRA_SERVER_PORT, profile.port)
            putExtra(EXTRA_CHANNEL, profile.channel)
            putExtra(EXTRA_REPEATER_ENABLED, repeaterEnabled)
            putExtra(EXTRA_RX_VOLUME_PERCENT, rxVolumePercent)
            putExtra(EXTRA_USE_BLUETOOTH_HEADSET, useBluetoothHeadset)
        }
        ContextCompat.startForegroundService(this, intent)
    }

    private fun loadSelectedServerProfile(): ServerProfile? {
        val store = ServerStore(this)
        val servers = store.load()
        if (servers.isEmpty()) return null
        val selectedName = store.getLastSelectedName()
        return servers.firstOrNull { it.name == selectedName } ?: servers.first()
    }

    private suspend fun runCaptureLoop() = withContext(Dispatchers.IO) {
        val frameSamples = currentFrameSamples()
        val minBuffer = AudioRecord.getMinBufferSize(
            currentCodecSampleRate(),
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        val preferredOption = microphoneConfigStore.getSelectedOption()
        val recorder = createRecorder(
            source = preferredOption.audioSource,
            frameSamples = frameSamples,
            minBuffer = minBuffer,
        ) ?: createRecorder(
            source = MediaRecorder.AudioSource.MIC,
            frameSamples = frameSamples,
            minBuffer = minBuffer,
        ) ?: run {
            transmitting.set(false)
            broadcastStatus(currentSignalByte())
            return@withContext
        }
        disableRecordPreprocessing(recorder.audioSessionId)

        val readBuffer = ShortArray(frameSamples)
        val txBuffer = ShortArray(frameSamples)
        var txFill = 0
        val frameNs = currentPacketMs().toLong() * 1_000_000L
        var nextFrameAtNs = System.nanoTime()
        try {
            recorder.startRecording()
            while (transmitting.get() && isActive) {
                val read = recorder.read(readBuffer, 0, readBuffer.size, AudioRecord.READ_BLOCKING)
                if (read <= 0) continue
                var srcPos = 0
                while (srcPos < read) {
                    val toCopy = minOf(frameSamples - txFill, read - srcPos)
                    System.arraycopy(readBuffer, srcPos, txBuffer, txFill, toCopy)
                    txFill += toCopy
                    srcPos += toCopy
                    if (txFill == frameSamples) {
                        val sleepNs = nextFrameAtNs - System.nanoTime()
                        if (sleepNs > 0L) {
                            delay((sleepNs / 1_000_000L).coerceAtLeast(1L))
                        } else if (sleepNs < -frameNs * 2) {
                            nextFrameAtNs = System.nanoTime()
                        }
                        currentSignalByte()
                        relay().pushTxPcm(txBuffer)
                        txFill = 0
                        nextFrameAtNs += frameNs
                    }
                }
            }
            recorder.stop()
        } finally {
            recorder.release()
            drainPendingUdpNetworkRecreate()
        }
    }

    private suspend fun streamRogerBeep(beepPcm: ShortArray) {
        streamGeneratedSignal(beepPcm)
    }

    private suspend fun streamGeneratedSignal(pcmSignal: ShortArray) {
        var offset = 0
        val frameSamples = currentFrameSamples()
        val frameNs = currentPacketMs().toLong() * 1_000_000L
        var nextFrameAtNs = System.nanoTime()
        while (offset < pcmSignal.size) {
            val frame = ShortArray(frameSamples)
            val end = (offset + frameSamples).coerceAtMost(pcmSignal.size)
            val count = end - offset
            if (count > 0) {
                System.arraycopy(pcmSignal, offset, frame, 0, count)
            }
            currentSignalByte()
            relay().pushTxPcm(frame)
            offset = end
            nextFrameAtNs += frameNs
            val sleepNs = nextFrameAtNs - System.nanoTime()
            if (sleepNs > 0L) {
                delay((sleepNs / 1_000_000L).coerceAtLeast(1L))
            } else if (sleepNs < -frameNs * 2) {
                // If scheduler/CPU lag caused severe drift, restart pacing from now.
                nextFrameAtNs = System.nanoTime()
            }
        }
    }

    private suspend fun playLocalRogerPcm(pcm: ShortArray, sampleRate: Int) = withContext(Dispatchers.IO) {
        playLocalSignalPcm(pcm, sampleRate, 0.0)
    }

    private suspend fun playLocalSignalPcm(pcm: ShortArray, sampleRate: Int, gainDb: Double) = withContext(Dispatchers.IO) {
        if (pcm.isEmpty()) return@withContext
        val playPcm = applyGainDb(pcm, gainDb)
        val track = AudioTrack(
            AudioManager.STREAM_MUSIC,
            sampleRate,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
            (playPcm.size * 2).coerceAtLeast(2),
            AudioTrack.MODE_STATIC,
        )
        try {
            val written = track.write(playPcm, 0, playPcm.size)
            if (written <= 0) return@withContext
            track.play()

            val expected = written.coerceAtLeast(1)
            val timeoutAt = System.currentTimeMillis() + 2000L
            while (isActive && System.currentTimeMillis() < timeoutAt) {
                if (track.playbackHeadPosition >= expected) {
                    break
                }
                delay(5L)
            }
        } finally {
            runCatching { track.stop() }
            track.release()
        }
    }

    private fun applyGainDb(pcm: ShortArray, gainDb: Double): ShortArray {
        if (gainDb == 0.0) return pcm
        val gain = Math.pow(10.0, gainDb / 20.0)
        val out = ShortArray(pcm.size)
        for (i in pcm.indices) {
            val v = (pcm[i] * gain).toInt()
            out[i] = v.coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort()
        }
        return out
    }

    private fun applyRxVolume(pcm: ShortArray): ShortArray {
        val percent = rxVolumePercent.coerceIn(RxVolumeStore.MIN_RX_VOLUME_PERCENT, RxVolumeStore.MAX_RX_VOLUME_PERCENT)
        if (percent == RxVolumeStore.DEFAULT_RX_VOLUME_PERCENT) return pcm
        val gain = percent / 100.0
        val out = ShortArray(pcm.size)
        for (i in pcm.indices) {
            val sample = (pcm[i] * gain).toInt()
            out[i] = sample.coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort()
        }
        return out
    }

    private fun prependSignal(prefix: ShortArray, main: ShortArray): ShortArray {
        if (prefix.isEmpty()) return main
        if (main.isEmpty()) return prefix
        val out = ShortArray(prefix.size + main.size)
        System.arraycopy(prefix, 0, out, 0, prefix.size)
        System.arraycopy(main, 0, out, prefix.size, main.size)
        return out
    }

    private fun resampleLinear(input: ShortArray, srcRate: Int, dstRate: Int): ShortArray {
        if (input.isEmpty()) return input
        if (srcRate == dstRate) return input
        val outSize = ((input.size.toDouble() * dstRate) / srcRate).toInt().coerceAtLeast(1)
        val out = ShortArray(outSize)
        val step = srcRate.toDouble() / dstRate
        for (i in 0 until outSize) {
            val pos = i * step
            val idx = pos.toInt().coerceIn(0, input.lastIndex)
            val frac = pos - idx
            val next = (idx + 1).coerceAtMost(input.lastIndex)
            val v = (input[idx] * (1.0 - frac) + input[next] * frac).toInt()
            out[i] = v.coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort()
        }
        return out
    }

    private fun loadWavPcmFromRaw(resourceId: Int): WavPcm {
        return runCatching {
            resources.openRawResource(resourceId).use { input ->
                val bytes = input.readBytes()
                if (bytes.size < 44) return WavPcm(shortArrayOf(), DEFAULT_SAMPLE_RATE)
                if (String(bytes, 0, 4) != "RIFF" || String(bytes, 8, 4) != "WAVE") {
                    return WavPcm(shortArrayOf(), DEFAULT_SAMPLE_RATE)
                }
                var offset = 12
                var srcRate = DEFAULT_SAMPLE_RATE
                var dataStart = -1
                var dataSize = 0
                while (offset + 8 <= bytes.size) {
                    val chunkId = String(bytes, offset, 4)
                    val chunkSize = ByteBuffer.wrap(bytes, offset + 4, 4).order(ByteOrder.LITTLE_ENDIAN).int
                    val payloadStart = offset + 8
                    if (chunkId == "fmt " && chunkSize >= 16 && payloadStart + 16 <= bytes.size) {
                        srcRate = ByteBuffer.wrap(bytes, payloadStart + 4, 4).order(ByteOrder.LITTLE_ENDIAN).int
                    }
                    if (chunkId == "data") {
                        dataStart = payloadStart
                        dataSize = chunkSize
                        break
                    }
                    offset = payloadStart + chunkSize + (chunkSize and 1)
                }
                if (dataStart < 0 || dataSize <= 0 || dataStart + dataSize > bytes.size) {
                    return WavPcm(shortArrayOf(), DEFAULT_SAMPLE_RATE)
                }
                val sampleCount = dataSize / 2
                val out = ShortArray(sampleCount)
                var p = dataStart
                for (i in 0 until sampleCount) {
                    out[i] = ByteBuffer.wrap(bytes, p, 2).order(ByteOrder.LITTLE_ENDIAN).short
                    p += 2
                }
                val safeRate = srcRate.takeIf { it in 4000..192000 } ?: DEFAULT_SAMPLE_RATE
                WavPcm(out, safeRate)
            }
        }.getOrDefault(WavPcm(shortArrayOf(), DEFAULT_SAMPLE_RATE))
    }

    private data class WavPcm(val samples: ShortArray, val sampleRate: Int)

    private fun generateRogerFromSelectedPattern(sampleRate: Int): ShortArray {
        val selected = rogerPatternStore.getSelectedPattern()
        if (selected.points.isEmpty()) return shortArrayOf()
        return generateSignalFromPattern(sampleRate, selected.points, appendTail = true)
    }

    private fun generateCallFromSelectedPattern(sampleRate: Int): ShortArray {
        val selected = callingPatternStore.getSelectedPattern()
        val segments = selected.expandedCallingPoints()
        if (segments.isEmpty()) return shortArrayOf()
        return generateSignalFromPattern(sampleRate, segments, appendTail = false)
    }

    private fun generateSignalFromPattern(
        sampleRate: Int,
        segments: List<ru.outsidepro_arts.owalkie.model.RogerPoint>,
        appendTail: Boolean,
    ): ShortArray {
        val tailSamples = (sampleRate * ROGER_TAIL_MS) / 1000
        val totalSamples = segments.sumOf { (sampleRate * it.durationMs) / 1000 } + if (appendTail) tailSamples else 0
        val out = ShortArray(totalSamples.coerceAtLeast(1))
        var idx = 0
        var phase = 0.0
        for (seg in segments) {
            val n = ((sampleRate * seg.durationMs) / 1000).coerceAtLeast(1)
            val isPause = seg.freqHz <= 0.0
            val phaseStep = if (isPause) 0.0 else 2.0 * PI * seg.freqHz / sampleRate
            for (i in 0 until n) {
                val envPos = i.toDouble() / n
                val env = when {
                    envPos < 0.08 -> envPos / 0.08
                    envPos > 0.92 -> (1.0 - envPos) / 0.08
                    else -> 1.0
                }
                val sample = if (isPause) 0.0 else sin(phase) * env * 0.26
                if (idx < out.size) {
                    out[idx] = (sample * Short.MAX_VALUE).toInt().toShort()
                    idx++
                }
                phase += phaseStep
            }
        }
        return if (idx == out.size) out else out.copyOf(idx)
    }


    private fun disableRecordPreprocessing(audioSessionId: Int) {
        runCatching {
            if (NoiseSuppressor.isAvailable()) {
                NoiseSuppressor.create(audioSessionId)?.let { ns ->
                    ns.enabled = false
                    ns.release()
                }
            }
        }
        runCatching {
            if (AcousticEchoCanceler.isAvailable()) {
                AcousticEchoCanceler.create(audioSessionId)?.let { aec ->
                    aec.enabled = false
                    aec.release()
                }
            }
        }
        runCatching {
            if (AutomaticGainControl.isAvailable()) {
                AutomaticGainControl.create(audioSessionId)?.let { agc ->
                    agc.enabled = false
                    agc.release()
                }
            }
        }
    }

    private fun ensureSignalMonitorLoop() {
        if (signalMonitorJob?.isActive == true) return
        signalMonitorJob = serviceScope.launch {
            while (isActive) {
                // Re-evaluate headset availability continuously so checked mode
                // auto-applies when headset connects and auto-falls back when disconnected.
                enforceAudioRoutePolicy()
                val signal = currentSignalByte()
                syncRelayReadyFromNativeIfNeeded()
                maybeRefreshNetworkReachableWhileReconnecting()
                broadcastStatus(signal)
                val loopDelayMs = if (activityFocused) {
                    SIGNAL_POLL_INTERVAL_FOREGROUND_MS
                } else {
                    SIGNAL_POLL_INTERVAL_BACKGROUND_MS
                }
                delay(loopDelayMs)
            }
        }
    }

    private fun registerNetworkCallback() {
        if (networkCallbackRegistered) return
        val request = NetworkRequest.Builder()
            .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
            .build()
        runCatching {
            connectivityManager.registerNetworkCallback(request, networkCallback)
            networkCallbackRegistered = true
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                val n = connectivityManager.activeNetwork
                if (n != null) {
                    activeCellularOrWifi = n
                }
            }
            updateNetworkReachableFromCapabilities("registerNetworkCallback")
        }
    }

    private fun unregisterNetworkCallback() {
        if (!networkCallbackRegistered) return
        runCatching {
            connectivityManager.unregisterNetworkCallback(networkCallback)
        }
        networkCallbackRegistered = false
    }

    private fun bindProcessToActiveNetwork() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return
        }
        OwalkieNative.ensureLoaded()
        val network = activeCellularOrWifi ?: connectivityManager.activeNetwork
        val handle = network?.networkHandle ?: 0L
        val rc = OwalkieNative.nativeBindProcessNetwork(handle)
        Log.i(TAG, "bindProcessNetwork handle=$handle rc=$rc network=${network != null}")
    }

    private fun networkCapabilitiesValidated(caps: NetworkCapabilities): Boolean {
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
            caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED) &&
            caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_SUSPENDED)
    }

    private fun updateNetworkReachableFromCapabilities(reason: String) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            if (!lastNetworkValidated.getAndSet(true)) {
                onNetworkValidated(true, reason)
            }
            return
        }
        val network = activeCellularOrWifi ?: connectivityManager.activeNetwork
        if (network == null) {
            if (lastNetworkValidated.getAndSet(false)) {
                Log.i(TAG, "networkValidated=false ($reason/noActiveNetwork)")
            }
            return
        }
        val caps = connectivityManager.getNetworkCapabilities(network) ?: run {
            if (lastNetworkValidated.getAndSet(false)) {
                Log.i(TAG, "networkValidated=false ($reason/noCapabilities)")
            }
            return
        }
        val validated = networkCapabilitiesValidated(caps)
        if (validated == lastNetworkValidated.get()) {
            return
        }
        lastNetworkValidated.set(validated)
        onNetworkValidated(validated, reason)
    }

    private fun maybeRefreshNetworkReachableWhileReconnecting() {
        if (!desiredConnection.get() || relayPausedForCellularCall.get()) return
        if (relayTransportConnected()) return
        updateNetworkReachableFromCapabilities("signalMonitor")
    }

    private fun onNetworkValidated(validated: Boolean, reason: String) {
        Log.i(TAG, "networkValidated=$validated ($reason)")
        if (!validated || !desiredConnection.get()) return
        bindProcessToActiveNetwork()
        if (relayTransportConnecting()) {
            return
        }
        if (transmitting.get() || rogerStreaming.get() || callStreaming.get()) {
            pendingUdpNetworkRecreate.set(true)
            return
        }
        relay().punchNat()
    }

    private fun drainPendingUdpNetworkRecreate() {
        if (!pendingUdpNetworkRecreate.getAndSet(false)) return
        if (!desiredConnection.get()) return
        if (transmitting.get() || rogerStreaming.get() || callStreaming.get()) {
            pendingUdpNetworkRecreate.set(true)
            return
        }
        relay().punchNat()
    }

    private fun clearServerSessionControlState() {
        stopPttLockDisplayTicks()
        serverRxBroadcastActive.set(false)
        serverPttLocked.set(false)
        pttLockDisplaySec.set(0)
    }

    private fun stopPttLockDisplayTicks() {
        pttLockDisplayTickRunnable?.let { mainHandler.removeCallbacks(it) }
        pttLockDisplayTickRunnable = null
    }

    private fun schedulePttLockDisplayTicks() {
        stopPttLockDisplayTicks()
        val tick = object : Runnable {
            override fun run() {
                if (!serverPttLocked.get()) return
                val v = pttLockDisplaySec.get()
                if (v <= 0) return
                pttLockDisplaySec.set(v - 1)
                broadcastStatus(currentSignalByte())
                pttLockDisplayTickRunnable = this
                mainHandler.postDelayed(this, 1000L)
            }
        }
        pttLockDisplayTickRunnable = tick
        mainHandler.postDelayed(tick, 1000L)
    }

    private fun forceAbortAllOutgoingForPttLock() {
        stopTxCountdownFromServer()
        val hadStream = transmitting.get() || rogerStreaming.get() || callStreaming.get()
        if (transmitting.get()) {
            transmitting.set(false)
            txJob?.cancel()
            txLoopRunning.set(false)
        }
        if (rogerStreaming.getAndSet(false)) {
            rogerJob?.cancel()
            rogerJob = null
        }
        if (callStreaming.getAndSet(false)) {
            callSignalJob?.cancel()
            callSignalJob = null
        }
        localRogerPlaybackJob?.cancel()
        localRogerPlaybackJob = null
        localCallPlaybackJob?.cancel()
        localCallPlaybackJob = null
        localPttPressPlaybackJob?.cancel()
        localPttPressPlaybackJob = null
        if (hadStream) {
            sendTxEof()
        }
        releaseVoiceProfileIfIdle()
    }

    private fun broadcastStatus(signal: Int) {
        refreshParallelTxCollisionState()
        updateForegroundNotification()
        val statusIntent = Intent(ACTION_STATUS).apply {
            setPackage(packageName)
            putExtra(EXTRA_SIGNAL, signal.coerceIn(0, 255))
            putExtra(EXTRA_WS_CONNECTED, relayTransportConnected())
            putExtra(EXTRA_WS_CONNECTING, relayTransportConnecting())
            putExtra(EXTRA_RELAY_PAUSED_PHONE_CALL, relayPausedForCellularCall.get())
            putExtra(EXTRA_UDP_READY, relay().isUdpReady)
            putExtra(EXTRA_PROTOCOL_ERROR, protocolError)
            putExtra(EXTRA_BUSY_MODE, busyMode)
            putExtra(EXTRA_PTT_SERVER_LOCKED, serverPttLocked.get())
            putExtra(EXTRA_PTT_LOCK_DISPLAY_SEC, pttLockDisplaySec.get().coerceAtLeast(0))
            putExtra(EXTRA_RX_ACTIVE, serverRxBroadcastActive.get())
            putExtra(EXTRA_TX_ACTIVE, transmitting.get() || rogerStreaming.get() || callStreaming.get())
            putExtra(EXTRA_PARALLEL_TX_COLLISION, parallelTxCollisionActive.get())
            putExtra(EXTRA_CALL_ACTIVE, callStreaming.get())
            putExtra(EXTRA_PTT_BURST_PRESS_BLOCKED, pttReleaseBurstPressBlocked.get())
            putExtra(EXTRA_DEBUG_UDP_RECOVERY_COUNT, udpRecoveryCount.get())
            putExtra(EXTRA_DEBUG_LAST_UDP_GAP_MS, lastObservedUdpGapMs.get())
        }
        sendBroadcast(statusIntent)
    }

    private fun syncRelayReadyFromNativeIfNeeded() {
        if (!desiredConnection.get() || relayPausedForCellularCall.get()) return
        if (relay().syncTransportStateFromNative() || relay().resyncReadyFromNative()) {
            broadcastStatus(currentSignalByte())
        }
    }

    private fun publishRelayStatus() {
        if (desiredConnection.get() && !relayPausedForCellularCall.get()) {
            bindProcessToActiveNetwork()
            relay().syncTransportStateFromNative()
            if (relayTransportConnected()) {
                relay().punchNat()
            }
        }
        broadcastStatus(currentSignalByte())
    }

    private fun setRepeaterMode(enabled: Boolean) {
        repeaterEnabled = enabled
        relay().setRepeater(enabled)
    }

    private fun sendTxEof() {
        relay().txEnd()
        syncRelayPowerAfterTx()
    }

    private fun syncRelayPowerAfterTx() {
        applyRelayPowerProfile()
    }

    /** Keep foreground keepalive while user wants a live session, even if Activity is hidden. */
    private fun applyRelayPowerProfile() {
        val relay = relay()
        when {
            transmitting.get() || rogerStreaming.get() || callStreaming.get() -> relay.setPowerActiveTx()
            desiredConnection.get() -> relay.setPowerForeground()
            else -> relay.setPowerBackground()
        }
    }

    private fun normalizePacketMs(value: Int): Int {
        return when (value) {
            10, 20, 40, 60 -> value
            else -> DEFAULT_PACKET_MS
        }
    }

    private fun normalizeSampleRate(value: Int): Int {
        return when (value) {
            8000, 12000, 16000, 24000, 48000 -> value
            else -> DEFAULT_SAMPLE_RATE
        }
    }

    private fun applyWelcomeFields(
        protocolVersion: Int,
        serverSessionId: Long,
        rawSampleRate: Int,
        packetMsInput: Int,
        busyModeInput: Boolean,
    ) {
        if (protocolVersion != PROTOCOL_VERSION) {
            protocolError = true
            relayPausedForCellularCall.set(false)
            desiredConnection.set(false)
            requestNativeRelayDisconnect()
            broadcastStatus(currentSignalByte())
            return
        }
        if (rawSampleRate < 0 || rawSampleRate != normalizeSampleRate(rawSampleRate)) {
            protocolError = true
            relayPausedForCellularCall.set(false)
            desiredConnection.set(false)
            requestNativeRelayDisconnect()
            broadcastStatus(currentSignalByte())
            return
        }
        val previousSampleRate = serverSampleRate
        val previousPacketMs = packetMs
        val newSession = (serverSessionId != 0L && serverSessionId != lastWelcomeSessionId)
        sessionId.set(serverSessionId)
        packetMs = normalizePacketMs(packetMsInput)
        if (serverSessionId != 0L) {
            lastWelcomeSessionId = serverSessionId
        }
        if (rawSampleRate != serverSampleRate) {
            serverSampleRate = rawSampleRate
        }
        if (previousSampleRate != serverSampleRate || previousPacketMs != packetMs || newSession) {
            if (::rxJitterBuffer.isInitialized) {
                rxJitterBuffer.resetCodec()
            }
        }
        busyMode = busyModeInput
        clearServerSessionControlState()
        channelBound = true
        if (!desiredConnection.get()) {
            requestNativeRelayDisconnect()
            return
        }
        syncPttMediaSession()
        broadcastStatus(currentSignalByte())
    }

    private fun currentCodecSampleRate(): Int = normalizeSampleRate(serverSampleRate)

    private fun currentPacketMs(): Int = normalizePacketMs(packetMs)

    private fun currentFrameSamples(): Int = (currentCodecSampleRate() * currentPacketMs()) / 1000

    private fun readServerPortFromIntent(intent: Intent): Int {
        var p = intent.getIntExtra(EXTRA_SERVER_PORT, -1)
        if (p in 1..65535) return p
        p = intent.getIntExtra(EXTRA_WS_PORT, -1)
        if (p in 1..65535) return p
        p = intent.getIntExtra(EXTRA_UDP_PORT, -1)
        if (p in 1..65535) return p
        return -1
    }

    private fun applyConfigFromIntent(intent: Intent?): Boolean {
        if (intent == null) return false
        val newHost = intent.getStringExtra(EXTRA_SERVER_HOST)?.trim().orEmpty()
        val fallbackPort = readServerPortFromIntent(intent)
        val newChannel = intent.getStringExtra(EXTRA_CHANNEL)?.trim().orEmpty()
        val newRepeaterEnabled = intent.getBooleanExtra(EXTRA_REPEATER_ENABLED, repeaterEnabled)
        val incomingRxVolume = intent.getIntExtra(EXTRA_RX_VOLUME_PERCENT, rxVolumePercent)
        val incomingUseBluetoothHeadset = intent.getBooleanExtra(EXTRA_USE_BLUETOOTH_HEADSET, useBluetoothHeadset)
        val parsedEndpoint = parseServerEndpoint(newHost, fallbackPort)

        if (parsedEndpoint == null || newChannel.isBlank()) {
            return false
        }

        synchronized(configLock) {
            updateRxVolumePercent(incomingRxVolume)
            useBluetoothHeadset = incomingUseBluetoothHeadset
            bluetoothHeadsetRouteStore.setEnabled(incomingUseBluetoothHeadset)
            val changed = serverHost != parsedEndpoint.host ||
                serverPort != parsedEndpoint.port ||
                channel != newChannel ||
                repeaterEnabled != newRepeaterEnabled ||
                wsSecure != parsedEndpoint.wsSecure
            if (!changed) return false

            serverHost = parsedEndpoint.host
            serverPort = parsedEndpoint.port
            channel = newChannel
            repeaterEnabled = newRepeaterEnabled
            wsSecure = parsedEndpoint.wsSecure
            sessionId.set(0)
            return true
        }
    }

    private fun updateRxVolumePercent(percent: Int) {
        val safe = percent.coerceIn(RxVolumeStore.MIN_RX_VOLUME_PERCENT, RxVolumeStore.MAX_RX_VOLUME_PERCENT)
        rxVolumePercent = safe
        rxVolumeStore.setPercent(safe)
    }

    private fun updateBluetoothHeadsetMode(enabled: Boolean) {
        useBluetoothHeadset = enabled
        bluetoothHeadsetRouteStore.setEnabled(enabled)
        enforceAudioRoutePolicy()
    }

    private fun updateActivityFocus(focused: Boolean) {
        activityFocused = focused
        lastSignalRefreshAtNs.set(0L)
        enforceAudioRoutePolicy()
        applyRelayPowerProfile()
        if (focused) {
            publishRelayStatus()
        }
        syncPttMediaSession()
    }

    private fun shouldOfferMediaButtonCapture(): Boolean {
        return pttHardwareKeyStore.isToggleModeEnabled() &&
            pttHardwareKeyStore.isMediaButtonPttEnabled() &&
            isRelaySessionReady() &&
            !protocolError &&
            !serverPttLocked.get()
    }

    private fun handleMediaButtonPttToggle() {
        if (!shouldOfferMediaButtonCapture()) return
        if (pttReleaseBurstPressBlocked.get()) {
            schedulePttReleaseBurstDecay()
            broadcastStatus(currentSignalByte())
            return
        }
        if (transmitting.get()) {
            onPttRelease()
        } else {
            onPttPress()
        }
    }

    private fun syncPttMediaSession() {
        if (!shouldOfferMediaButtonCapture()) {
            pttMediaSessionController?.release()
            pttMediaSessionController = null
            lastRxPlaybackWriteNs.set(0L)
            return
        }
        if (pttMediaSessionController == null) {
            pttMediaSessionController = PttMediaSessionController(this, mainHandler, mediaButtonPttToggleRunnable)
        }
        pttMediaSessionController?.ensureActiveAndRefreshState()
    }

    private fun maybeAnchorMediaSessionOnRxPlaybackWrite(nowNs: Long) {
        if (!shouldOfferMediaButtonCapture()) return
        val ctrl = pttMediaSessionController ?: return
        val lastAnchor = lastRxMediaSessionAnchorNs.get()
        if (nowNs - lastAnchor < MEDIA_SESSION_RX_ANCHOR_MIN_INTERVAL_NS) return
        if (!lastRxMediaSessionAnchorNs.compareAndSet(lastAnchor, nowNs)) return
        mainHandler.post { ctrl.refreshPlaybackStateAnchor() }
    }

    private fun isRelaySessionReady(): Boolean = relay().isConnected

    private fun shouldHoldBluetoothCommunicationProfile(): Boolean {
        return useBluetoothHeadset &&
            desiredConnection.get() &&
            activityFocused &&
            isBluetoothHeadsetAvailable()
    }

    private fun enforceAudioRoutePolicy() {
        if (shouldHoldBluetoothCommunicationProfile()) {
            ensureVoiceAudioProfile(preferBluetoothMic = true)
            return
        }
        if (!activityFocused && useBluetoothHeadset) {
            // Explicitly release communication profile when app goes background.
            restoreMediaAudioProfile()
            return
        }
        releaseVoiceProfileIfIdle()
    }

    private data class ParsedServerEndpoint(
        val host: String,
        val port: Int,
        val wsSecure: Boolean,
    )

    private fun parseServerEndpoint(rawHost: String, fallbackPort: Int): ParsedServerEndpoint? {
        var value = rawHost.trim()
        if (value.isBlank()) return null
        var secure = false
        val lowered = value.lowercase()
        when {
            lowered.startsWith("wss://") -> {
                secure = true
                value = value.substring(6)
            }
            lowered.startsWith("https://") -> {
                secure = true
                value = value.substring(8)
            }
            lowered.startsWith("ws://") -> value = value.substring(5)
            lowered.startsWith("http://") -> value = value.substring(7)
        }
        value = value.substringBefore('/').substringBefore('?').substringBefore('#').trim()
        value = value.substringAfterLast('@')
        if (value.isBlank()) return null

        var port = fallbackPort
        var host = value
        if (host.startsWith("[")) {
            val closing = host.indexOf(']')
            if (closing <= 0) return null
            val ipv6 = host.substring(1, closing).trim()
            val rest = host.substring(closing + 1)
            if (rest.startsWith(":")) {
                val parsedPort = rest.substring(1).toIntOrNull() ?: return null
                port = parsedPort
            }
            host = ipv6
        } else {
            val colonCount = host.count { it == ':' }
            if (colonCount == 1) {
                val idx = host.lastIndexOf(':')
                val maybePort = host.substring(idx + 1).toIntOrNull()
                if (maybePort != null) {
                    port = maybePort
                    host = host.substring(0, idx)
                }
            }
        }
        if (host.isBlank() || port !in 1..65535) return null
        return ParsedServerEndpoint(host = host, port = port, wsSecure = secure)
    }

    /**
     * Drops active relay transport and TX/Roger/Call jobs.
     * @param clearUserIntent When true, clears [desiredConnection] (user or fatal protocol disconnect).
     */
    private fun prepareTransportTeardown(clearUserIntent: Boolean): Long {
        resetPttReleaseBurstGuard()
        stopTxCountdownFromServer()
        transmitting.set(false)
        rogerStreaming.set(false)
        callStreaming.set(false)
        txJob?.cancel()
        txJob = null
        rogerJob?.cancel()
        rogerJob = null
        callSignalJob?.cancel()
        callSignalJob = null
        localRogerPlaybackJob?.cancel()
        localRogerPlaybackJob = null
        localCallPlaybackJob?.cancel()
        localCallPlaybackJob = null
        if (clearUserIntent) {
            desiredConnection.set(false)
            stopClientReconnectLoop()
        } else {
            // Pause reconnect attempts while native teardown runs (server switch / cellular pause).
            stopClientReconnectLoop()
        }
        channelBound = false
        protocolError = false
        busyMode = false
        clearServerSessionControlState()
        lastInboundUdpAtNs.set(0L)
        lastRxDuringParallelTxNs.set(0L)
        parallelTxCollisionActive.set(false)
        syncParallelCollisionVibration(false)
        lastRxAudiblePlaybackWriteNs.set(0L)
        lastObservedUdpGapMs.set(0L)
        lastWelcomeSessionId = 0L
        sessionId.set(0)
        resetRelayCodecState()
        pendingUdpNetworkRecreate.set(false)
        udpRecoveryCount.set(0L)
        resetNativeRelayUiTransportState()
        restoreMediaAudioProfile()
        return relay().detachLocalTransportState()
    }

    private fun tearDownTransport(clearUserIntent: Boolean) {
        val detachedSessionId = prepareTransportTeardown(clearUserIntent)
        beginNativeTeardown(detachedSessionId)
    }

    private suspend fun tearDownTransportAndAwait(clearUserIntent: Boolean) {
        val detachedSessionId = prepareTransportTeardown(clearUserIntent)
        beginNativeTeardown(detachedSessionId).join()
        nativeDisconnectJob = null
    }

    private fun cancelConnection() {
        relayPausedForCellularCall.set(false)
        serviceScope.launch(Dispatchers.IO) {
            stopClientReconnectLoop()
            waitForNativeConnectIdle()
            tearDownTransportAndAwait(clearUserIntent = true)
            withContext(Dispatchers.Main) {
                broadcastStatus(currentSignalByte())
            }
        }
    }

    private fun pauseRelayTransportForCellularCall() {
        if (!phoneCallRelayPauseStore.isPauseDuringCallEnabled()) return
        if (!desiredConnection.get()) return
        if (!relayPausedForCellularCall.compareAndSet(false, true)) return
        tearDownTransport(clearUserIntent = false)
        broadcastStatus(currentSignalByte())
    }

    private fun resumeRelayAfterCellularCallIfNeeded() {
        if (!relayPausedForCellularCall.compareAndSet(true, false)) return
        if (!desiredConnection.get()) return
        startCore(null)
    }

    private fun onTelephonyCallStateChanged(state: Int) {
        when (state) {
            TelephonyManager.CALL_STATE_OFFHOOK ->
                serviceScope.launch { pauseRelayTransportForCellularCall() }
            TelephonyManager.CALL_STATE_IDLE ->
                serviceScope.launch { resumeRelayAfterCellularCallIfNeeded() }
        }
    }

    private fun registerTelephonyForRelayPause() {
        val tm = getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val cb = object : TelephonyCallback(), TelephonyCallback.CallStateListener {
                override fun onCallStateChanged(state: Int) {
                    onTelephonyCallStateChanged(state)
                }
            }
            telephonyCallStateCallback = cb
            runCatching {
                tm.registerTelephonyCallback(ContextCompat.getMainExecutor(this), cb)
            }
        } else {
            @Suppress("DEPRECATION")
            val listener = object : PhoneStateListener() {
                @Suppress("DEPRECATION")
                override fun onCallStateChanged(state: Int, phoneNumber: String?) {
                    onTelephonyCallStateChanged(state)
                }
            }
            legacyPhoneStateListener = listener
            @Suppress("DEPRECATION")
            runCatching { tm.listen(listener, PhoneStateListener.LISTEN_CALL_STATE) }
        }
    }

    private fun unregisterTelephonyForRelayPause() {
        val tm = getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager ?: return
        telephonyCallStateCallback?.let { cb ->
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                runCatching { tm.unregisterTelephonyCallback(cb) }
            }
            telephonyCallStateCallback = null
        }
        legacyPhoneStateListener?.let { listener ->
            @Suppress("DEPRECATION")
            runCatching { tm.listen(listener, PhoneStateListener.LISTEN_NONE) }
            legacyPhoneStateListener = null
        }
    }

    private fun disconnectAndStopService() {
        beginFullShutdown(logTag = "disconnectAndStop", markAppExit = false) {
            finishServiceForeground()
            stopSelf()
        }
    }

    private fun exitApp() {
        beginFullShutdown(logTag = "exitApp", markAppExit = true) {
            finishServiceForeground()
            stopSelf()
            Log.i(TAG, "exitApp: service stopped")
        }
    }

    private fun beginFullShutdown(
        logTag: String,
        markAppExit: Boolean,
        onComplete: () -> Unit,
    ) {
        if (!fullShutdownStarted.compareAndSet(false, true)) {
            Log.i(TAG, "$logTag: shutdown already in progress")
            return
        }
        if (markAppExit) {
            exitRequested.set(true)
            isAppExiting.set(true)
        }
        Log.i(TAG, "$logTag: full shutdown started")
        Thread({
            runBlocking(Dispatchers.IO) {
                performFullShutdown(logTag)
            }
            mainHandler.post(onComplete)
        }, "owalkie-shutdown-$logTag").start()
    }

    private fun finishServiceForeground() {
        cancelServiceNotification()
        stopForeground(STOP_FOREGROUND_REMOVE)
    }

    private fun cancelServiceNotification() {
        runCatching {
            val nm = getSystemService(NOTIFICATION_SERVICE) as NotificationManager
            nm.cancel(NOTIFICATION_ID)
        }
    }

    private suspend fun performFullShutdown(logTag: String) {
        Log.i(TAG, "$logTag: performFullShutdown")
        relayPausedForCellularCall.set(false)
        desiredConnection.set(false)
        reconnectSettleUntilMs.set(0L)

        val reconnectJob = clientReconnectJob
        stopClientReconnectLoop()
        withTimeoutOrNull(3_000L) { reconnectJob?.join() }

        signalMonitorJob?.cancel()
        signalMonitorJob = null
        nativeDisconnectJob?.cancel()
        withTimeoutOrNull(12_000L) { nativeDisconnectJob?.join() }
        nativeDisconnectJob = null

        transmitting.set(false)
        rogerStreaming.set(false)
        callStreaming.set(false)
        txJob?.cancel()
        txJob = null
        rogerJob?.cancel()
        rogerJob = null
        callSignalJob?.cancel()
        callSignalJob = null
        localRogerPlaybackJob?.cancel()
        localRogerPlaybackJob = null
        localCallPlaybackJob?.cancel()
        localCallPlaybackJob = null
        localPttPressPlaybackJob?.cancel()
        localPttPressPlaybackJob = null

        stopTxCountdownFromServer()
        resetPttReleaseBurstGuard()
        syncParallelCollisionVibration(false)
        clearServerSessionControlState()

        OwalkieNative.ensureLoaded()
        OwalkieNative.nativeDisconnectAll()

        if (!waitForNativeConnectIdle()) {
            Log.w(TAG, "$logTag: native connect still in flight after wait; forcing teardown")
            nativeConnectInFlight.set(false)
        }

        val detachedSessionId = prepareTransportTeardown(clearUserIntent = true)
        tearDownNativeRelay(detachedSessionId)

        OwalkieNative.nativeDisconnectAllAndWait(APP_EXIT_SHUTDOWN_WAIT_MS)

        releaseRxPlaybackTrack()

        withContext(Dispatchers.Main) {
            pttMediaSessionController?.release()
            pttMediaSessionController = null
        }

        unregisterNetworkCallback()
        unregisterTelephonyForRelayPause()
        restoreMediaAudioProfile()

        serviceScope.cancel()
        Log.i(TAG, "$logTag: performFullShutdown done")
    }

    private fun createRecorder(source: Int, frameSamples: Int, minBuffer: Int): AudioRecord? {
        return runCatching {
            AudioRecord(
                source,
                currentCodecSampleRate(),
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                minBuffer.coerceAtLeast(frameSamples * 4),
            )
        }.getOrNull()?.takeIf { it.state == AudioRecord.STATE_INITIALIZED }
    }

    private fun ensureVoiceAudioProfile(preferBluetoothMic: Boolean = false) {
        runCatching {
            val bluetoothRouteAllowed = preferBluetoothMic && isBluetoothHeadsetAvailable()
            audioManager.mode = if (bluetoothRouteAllowed) AudioManager.MODE_IN_COMMUNICATION else AudioManager.MODE_NORMAL
            audioManager.isSpeakerphoneOn = false
            if (bluetoothRouteAllowed) {
                enableBluetoothInputRoute()
            } else {
                disableBluetoothInputRoute()
            }
            voiceProfileActive = true
        }
    }

    private fun restoreMediaAudioProfile() {
        runCatching {
            disableBluetoothInputRoute()
            audioManager.mode = AudioManager.MODE_NORMAL
            audioManager.isSpeakerphoneOn = false
            voiceProfileActive = false
        }
    }

    private fun releaseVoiceProfileIfIdle() {
        if (shouldHoldBluetoothCommunicationProfile()) {
            return
        }
        if (!transmitting.get() && !rogerStreaming.get() && !callStreaming.get()) {
            restoreMediaAudioProfile()
        }
    }

    private fun enableBluetoothInputRoute() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            runCatching {
                val btDevice = audioManager.availableCommunicationDevices.firstOrNull {
                    it.type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO || it.type == AudioDeviceInfo.TYPE_BLE_HEADSET
                }
                if (btDevice != null) {
                    audioManager.setCommunicationDevice(btDevice)
                } else {
                    audioManager.clearCommunicationDevice()
                }
            }
        } else {
            @Suppress("DEPRECATION")
            runCatching {
                audioManager.startBluetoothSco()
                audioManager.isBluetoothScoOn = true
            }
        }
    }

    private fun isBluetoothHeadsetAvailable(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            audioManager.availableCommunicationDevices.any {
                it.type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO || it.type == AudioDeviceInfo.TYPE_BLE_HEADSET
            }
        } else {
            audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS).any {
                it.type == AudioDeviceInfo.TYPE_BLUETOOTH_SCO
            }
        }
    }

    private fun disableBluetoothInputRoute() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            runCatching { audioManager.clearCommunicationDevice() }
        }
        @Suppress("DEPRECATION")
        runCatching {
            if (audioManager.isBluetoothScoOn) {
                audioManager.stopBluetoothSco()
                audioManager.isBluetoothScoOn = false
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun currentSignalByte(): Int {
        val nowNs = System.nanoTime()
        val refreshIntervalNs = if (activityFocused) {
            SIGNAL_REFRESH_INTERVAL_FOREGROUND_MS * 1_000_000L
        } else {
            SIGNAL_REFRESH_INTERVAL_BACKGROUND_MS * 1_000_000L
        }
        val lastNs = lastSignalRefreshAtNs.get()
        if (lastNs != 0L && (nowNs - lastNs) < refreshIntervalNs) {
            return OwalkieNative.nativeGetUplinkSignalByte()
        }

        OwalkieNative.ensureLoaded()
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        if (wifiManager != null && wifiManager.isWifiEnabled) {
            val rssi = wifiManager.connectionInfo?.rssi ?: -100
            OwalkieNative.nativeReportSignal(OwalkieNative.SIGNAL_WIFI, rssi)
        } else {
            OwalkieNative.nativeClearSignal(OwalkieNative.SIGNAL_WIFI)
        }
        val telephonyManager = getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager
        val cellLevel = runCatching { telephonyManager?.signalStrength?.level }.getOrNull()
        if (cellLevel != null) {
            OwalkieNative.nativeReportSignal(OwalkieNative.SIGNAL_CELL, cellLevel)
        } else {
            OwalkieNative.nativeClearSignal(OwalkieNative.SIGNAL_CELL)
        }
        val signal = OwalkieNative.nativeGetUplinkSignalByte()
        lastSignalRefreshAtNs.set(nowNs)
        return signal
    }

    // --- NativeRelayBridge.Host ---

    override fun applyWelcomeFromSessionInfo(info: OwalkieNative.SessionInfo) {
        serviceScope.launch(Dispatchers.Default) {
            applyWelcomeFields(
                protocolVersion = info.protocolVersion,
                serverSessionId = info.serverSessionId,
                rawSampleRate = info.sampleRate,
                packetMsInput = info.packetMs,
                busyModeInput = info.busyMode,
            )
        }
    }

    override fun onRelayConnectionLost(reason: String?) {
        if (relay().syncTransportStateFromNative() || relay().resyncReadyFromNative()) {
            Log.i(TAG, "connectionLost recovered from native reason=$reason")
            runOnMain { publishRelayStatus() }
            return
        }
        runOnMain {
            clearServerSessionControlState()
            broadcastStatus(currentSignalByte())
            syncPttMediaSession()
        }
    }

    override fun onRelayConnected(serverSessionId: Long, udpReady: Boolean) {
        lastRelayConnectedAtMs.set(System.currentTimeMillis())
        clearReconnectSettle()
        runOnMain {
            sessionId.set(serverSessionId)
            protocolError = false
            if (udpReady) {
                udpRecoveryCount.incrementAndGet()
            }
            currentSignalByte()
            relay().punchNat()
            applyRelayPowerProfile()
            broadcastStatus(currentSignalByte())
            syncPttMediaSession()
        }
    }

    override fun onRelayProtocolError() {
        runOnMain {
            protocolError = true
            relayPausedForCellularCall.set(false)
            desiredConnection.set(false)
            channelBound = false
            requestNativeRelayDisconnect()
            broadcastStatus(currentSignalByte())
            syncPttMediaSession()
        }
    }

    override fun onRelayDisconnected() {
        runOnMain {
            channelBound = false
            busyMode = false
            clearServerSessionControlState()
            broadcastStatus(currentSignalByte())
            syncPttMediaSession()
        }
    }

    override fun onRelayTxCountdownStart() {
        runOnMain { startTxCountdownFromServer() }
    }

    override fun onRelayTxStop() {
        runOnMain { forceStopTransmissionFromServer() }
    }

    override fun onRelayRxBroadcastStart(busyMode: Boolean) {
        runOnMain {
            serverRxBroadcastActive.set(true)
            this.busyMode = busyMode
            broadcastStatus(currentSignalByte())
        }
    }

    override fun onRelayRxBroadcastEnd() {
        runOnMain {
            serverRxBroadcastActive.set(false)
            broadcastStatus(currentSignalByte())
        }
    }

    override fun onRelayPttLock(displaySec: Int) {
        runOnMain {
            forceAbortAllOutgoingForPttLock()
            serverPttLocked.set(true)
            pttLockDisplaySec.set(displaySec.coerceAtLeast(0))
            schedulePttLockDisplayTicks()
            broadcastStatus(currentSignalByte())
        }
    }

    override fun onRelayPttUnlock() {
        runOnMain {
            stopPttLockDisplayTicks()
            serverPttLocked.set(false)
            pttLockDisplaySec.set(0)
            broadcastStatus(currentSignalByte())
        }
    }

    override fun onRelayRxPcm(pcm: ShortArray, sampleRate: Int, packetMs: Int) {
        processInboundRxPcm(pcm, sampleRate, packetMs)
    }

    override fun isActivityFocused(): Boolean = activityFocused
}

