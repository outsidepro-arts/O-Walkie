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
import android.net.wifi.WifiManager
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.telephony.TelephonyManager
import android.view.KeyEvent
import androidx.core.app.NotificationCompat
import ru.outsidepro_arts.owalkie.audio.OpusCodec
import ru.outsidepro_arts.owalkie.audio.OpusConfig
import ru.outsidepro_arts.owalkie.audio.OpusCodecFactory
import ru.outsidepro_arts.owalkie.model.CallingPatternStore
import ru.outsidepro_arts.owalkie.model.BluetoothHeadsetRouteStore
import ru.outsidepro_arts.owalkie.model.MicrophoneConfigStore
import ru.outsidepro_arts.owalkie.model.PttHardwareKeyStore
import ru.outsidepro_arts.owalkie.model.RogerPatternStore
import ru.outsidepro_arts.owalkie.model.RxVolumeStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.UnknownHostException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import kotlin.math.PI
import kotlin.math.sin
import kotlin.random.Random

class WalkieService : Service() {
    companion object {
        const val ACTION_START = "ru.outsidepro_arts.owalkie.action.START"
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
        const val ACTION_HARDWARE_PTT_KEY = "ru.outsidepro_arts.owalkie.action.HARDWARE_PTT_KEY"
        const val EXTRA_SIGNAL = "signal"
        const val EXTRA_WS_CONNECTED = "wsConnected"
        const val EXTRA_WS_CONNECTING = "wsConnecting"
        const val EXTRA_UDP_READY = "udpReady"
        const val EXTRA_PROTOCOL_ERROR = "protocolError"
        const val EXTRA_BUSY_MODE = "busyMode"
        const val EXTRA_BUSY_RX_ACTIVE = "busyRxActive"
        const val EXTRA_TX_ACTIVE = "txActive"
        const val EXTRA_CALL_ACTIVE = "callActive"
        const val EXTRA_SERVER_HOST = "serverHost"
        const val EXTRA_WS_PORT = "wsPort"
        const val EXTRA_UDP_PORT = "udpPort"
        const val EXTRA_CHANNEL = "channel"
        const val EXTRA_REPEATER_ENABLED = "repeaterEnabled"
        const val EXTRA_RX_VOLUME_PERCENT = "rxVolumePercent"
        const val EXTRA_USE_BLUETOOTH_HEADSET = "useBluetoothHeadset"
        const val EXTRA_ACTIVITY_FOCUSED = "activityFocused"
        const val EXTRA_HW_KEY_ACTION = "hwKeyAction"
        const val EXTRA_HW_KEY_REPEAT = "hwKeyRepeat"
        const val EXTRA_HW_KEY_CODE = "hwKeyCode"
        const val EXTRA_HW_SCAN_CODE = "hwScanCode"

        private const val NOTIFICATION_CHANNEL_ID = "owalkie_stream"
        private const val NOTIFICATION_ID = 101

        private const val DEFAULT_WS_HOST = ""
        private const val DEFAULT_WS_PORT = 8080
        private const val DEFAULT_UDP_PORT = 5000
        private const val DEFAULT_CHANNEL = "global"

        private const val DEFAULT_SAMPLE_RATE = 8000
        private const val LOCAL_PLAYBACK_SAMPLE_RATE = 44100
        private const val CHANNELS = 1
        private const val DEFAULT_PACKET_MS = 20
        private const val PROTOCOL_VERSION = 2
        private const val MAX_PACKET_SAMPLES = 48000 * 60 / 1000
        private const val ROGER_TAIL_MS = 40
        private const val CALL_LOCAL_GAIN_DB = -10.0
        private const val UDP_KEEPALIVE_IDLE_INTERVAL_SEC = 12L
        private const val UDP_KEEPALIVE_RECOVERY_INTERVAL_SEC = 6L
        private const val UDP_KEEPALIVE_RECOVERY_WINDOW_SEC = 90L
        private const val UDP_KEEPALIVE_JITTER_PERCENT = 15L
    }

    private val binder = Binder()
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private val okHttpClient = OkHttpClient.Builder().retryOnConnectionFailure(true).build()
    private var webSocket: WebSocket? = null
    private var wsReconnectJob: Job? = null
    private var signalMonitorJob: Job? = null
    private var udpKeepaliveJob: Job? = null
    private val wsConnected = AtomicBoolean(false)
    private val wsOpening = AtomicBoolean(false)
    private val wsRetryAttempt = AtomicInteger(0)
    private val desiredConnection = AtomicBoolean(false)
    private val configLock = Any()
    private val wsConnectLock = Any()

    @Volatile
    private var udpSocket: DatagramSocket? = null
    private val udpSocketLock = Any()

    private var udpReceiveJob: Job? = null
    private var txJob: Job? = null
    private var rogerJob: Job? = null
    private var callSignalJob: Job? = null
    private var localRogerPlaybackJob: Job? = null
    private var localCallPlaybackJob: Job? = null
    private var localPttPressPlaybackJob: Job? = null

    private val transmitting = AtomicBoolean(false)
    private val rogerStreaming = AtomicBoolean(false)
    private val callStreaming = AtomicBoolean(false)
    private val sessionId = AtomicLong(0L)
    private val seq = AtomicInteger(0)
    private val encodeLock = Any()
    private val rxResumeAtNs = AtomicLong(0L)
    private val busyRxActive = AtomicBoolean(false)
    private val busyLastRxAtNs = AtomicLong(0L)
    private val lastInboundUdpAtNs = AtomicLong(0L)
    private val lastTxCollisionVibrateAtNs = AtomicLong(0L)
    private val lastOutboundUdpAtNs = AtomicLong(0L)
    private val udpKeepaliveRecoveryUntilNs = AtomicLong(0L)

    @Volatile
    private var serverHost: String = DEFAULT_WS_HOST

    @Volatile
    private var wsPort: Int = DEFAULT_WS_PORT

    @Volatile
    private var udpPort: Int = DEFAULT_UDP_PORT

    @Volatile
    private var channel: String = DEFAULT_CHANNEL

    @Volatile
    private var targetUdpAddress: InetAddress? = null

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

    private lateinit var codec: OpusCodec
    private lateinit var audioManager: AudioManager
    private lateinit var rogerPatternStore: RogerPatternStore
    private lateinit var callingPatternStore: CallingPatternStore
    private lateinit var microphoneConfigStore: MicrophoneConfigStore
    private lateinit var bluetoothHeadsetRouteStore: BluetoothHeadsetRouteStore
    private lateinit var rxVolumeStore: RxVolumeStore
    private lateinit var pttHardwareKeyStore: PttHardwareKeyStore
    private var localPttPressPcm: ShortArray = shortArrayOf()
    private var localPttReleasePcm: ShortArray = shortArrayOf()
    @Volatile
    private var opusConfig: OpusConfig = OpusConfig()
    @Volatile
    private var voiceProfileActive: Boolean = false
    @Volatile
    private var rxVolumePercent: Int = RxVolumeStore.DEFAULT_RX_VOLUME_PERCENT
    @Volatile
    private var useBluetoothHeadset: Boolean = false
    @Volatile
    private var activityFocused: Boolean = true

    override fun onCreate() {
        super.onCreate()
        codec = OpusCodecFactory().create(DEFAULT_SAMPLE_RATE, CHANNELS, opusConfig)
        audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        rogerPatternStore = RogerPatternStore(this)
        callingPatternStore = CallingPatternStore(this)
        microphoneConfigStore = MicrophoneConfigStore(this)
        bluetoothHeadsetRouteStore = BluetoothHeadsetRouteStore(this)
        rxVolumeStore = RxVolumeStore(this)
        pttHardwareKeyStore = PttHardwareKeyStore(this)
        useBluetoothHeadset = bluetoothHeadsetRouteStore.isEnabled()
        rxVolumePercent = rxVolumeStore.getPercent()
        val pttPressWav = loadWavPcmFromRaw(R.raw.selfpttup_002)
        localPttPressPcm = resampleLinear(pttPressWav.samples, pttPressWav.sampleRate, LOCAL_PLAYBACK_SAMPLE_RATE)
        val pttReleaseWav = loadWavPcmFromRaw(R.raw.selfttdown_002)
        localPttReleasePcm = resampleLinear(pttReleaseWav.samples, pttReleaseWav.sampleRate, LOCAL_PLAYBACK_SAMPLE_RATE)
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                desiredConnection.set(true)
                startCore(intent)
            }
            ACTION_CANCEL_CONNECT -> cancelConnection()
            ACTION_DISCONNECT_AND_STOP -> disconnectAndStopService()
            ACTION_TOGGLE_CONNECTION -> {
                if (desiredConnection.get() || wsConnected.get()) {
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
            ACTION_EXIT_APP -> exitApp()
        }
        return START_STICKY
    }

    private fun handleHardwarePttKeyEvent(event: KeyEvent): Boolean {
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
        super.onDestroy()
        transmitting.set(false)
        txJob?.cancel()
        rogerJob?.cancel()
        callSignalJob?.cancel()
        localRogerPlaybackJob?.cancel()
        localCallPlaybackJob?.cancel()
        localPttPressPlaybackJob?.cancel()
        udpReceiveJob?.cancel()
        wsReconnectJob?.cancel()
        signalMonitorJob?.cancel()
        udpKeepaliveJob?.cancel()
        webSocket?.close(1000, "service destroyed")
        synchronized(udpSocketLock) {
            udpSocket?.close()
            udpSocket = null
        }
        restoreMediaAudioProfile()
        okHttpClient.dispatcher.executorService.shutdown()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        super.onTaskRemoved(rootIntent)
        if (!desiredConnection.get()) return
        val restartIntent = Intent(applicationContext, WalkieService::class.java).apply {
            action = ACTION_START
            putExtra(EXTRA_SERVER_HOST, serverHost)
            putExtra(EXTRA_WS_PORT, wsPort)
            putExtra(EXTRA_UDP_PORT, udpPort)
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
        ensureUdpSocket()
        wsReconnectJob?.cancel()
        wsReconnectJob = null
        if (changed) {
            connectWebSocket(force = true)
        } else {
            connectWebSocket()
        }
        ensurePlaybackLoop()
        ensureSignalMonitorLoop()
    }

    private fun startForegroundInternal() {
        ensureNotificationChannel()
        val isConnectingOrConnected = desiredConnection.get() || wsConnected.get()
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
        val isConnectingOrConnected = desiredConnection.get() || wsConnected.get()
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

    private fun connectWebSocket(force: Boolean = false) {
        if (!desiredConnection.get()) return
        synchronized(wsConnectLock) {
            if (!force && (webSocket != null || wsOpening.get())) return
            if (force) {
                // Force reconnect can otherwise overlap with stale callbacks
                // and lead to double audio frames (server has two active sessions).
                // Stop UDP keepalive tied to the old session and close the WS cleanly.
                stopUdpKeepaliveLoop()
                wsConnected.set(false)
                channelBound = false
                busyMode = false
                busyRxActive.set(false)

                runCatching { webSocket?.close(1001, "reconnect") }
                runCatching { webSocket?.cancel() }
                webSocket = null
                wsOpening.set(false)
            }
            wsOpening.set(true)
        }
        val wsUrl = wsUrl()
        if (wsUrl.isBlank()) {
            wsConnected.set(false)
            wsOpening.set(false)
            broadcastStatus(currentSignalByte())
            return
        }
        val request = runCatching { Request.Builder().url(wsUrl).build() }.getOrElse {
            wsConnected.set(false)
            wsOpening.set(false)
            broadcastStatus(currentSignalByte())
            return
        }
        webSocket = okHttpClient.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                if (this@WalkieService.webSocket !== webSocket) return
                wsOpening.set(false)
                wsConnected.set(true)
                wsRetryAttempt.set(0)
                packetMs = DEFAULT_PACKET_MS
                channelBound = false
                protocolError = false
                busyMode = false
                busyRxActive.set(false)
                broadcastStatus(currentSignalByte())
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                if (this@WalkieService.webSocket !== webSocket) return
                handleWsMessage(webSocket, text)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                if (this@WalkieService.webSocket !== webSocket) return
                wsOpening.set(false)
                wsConnected.set(false)
                channelBound = false
                busyMode = false
                busyRxActive.set(false)
                stopUdpKeepaliveLoop()
                this@WalkieService.webSocket = null
                if (desiredConnection.get()) {
                    scheduleWsReconnect()
                } else {
                    broadcastStatus(currentSignalByte())
                }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (this@WalkieService.webSocket !== webSocket) return
                wsOpening.set(false)
                wsConnected.set(false)
                channelBound = false
                busyMode = false
                busyRxActive.set(false)
                stopUdpKeepaliveLoop()
                this@WalkieService.webSocket = null
                if (desiredConnection.get()) {
                    scheduleWsReconnect()
                } else {
                    broadcastStatus(currentSignalByte())
                }
            }
        })
    }

    private fun handleWsMessage(ws: WebSocket, text: String) {
        runCatching {
            if (this@WalkieService.webSocket !== ws) return@runCatching
            val obj = JSONObject(text)
            when (obj.optString("type")) {
                "welcome" -> {
                    val serverProtocol = obj.optInt("protocolVersion", -1)
                    if (serverProtocol != PROTOCOL_VERSION) {
                        protocolError = true
                        desiredConnection.set(false)
                        wsConnected.set(false)
                        ws.close(1002, "incompatible protocol version")
                        broadcastStatus(currentSignalByte())
                        return@runCatching
                    }
                    if (!obj.has("sampleRate")) {
                        protocolError = true
                        desiredConnection.set(false)
                        wsConnected.set(false)
                        ws.close(1002, "missing sampleRate in welcome")
                        broadcastStatus(currentSignalByte())
                        return@runCatching
                    }
                    val rawSampleRate = obj.optInt("sampleRate", -1)
                    if (rawSampleRate != normalizeSampleRate(rawSampleRate)) {
                        protocolError = true
                        desiredConnection.set(false)
                        wsConnected.set(false)
                        ws.close(1002, "unsupported sampleRate in welcome")
                        broadcastStatus(currentSignalByte())
                        return@runCatching
                    }
                    val previousSampleRate = serverSampleRate
                    val previousPacketMs = packetMs
                    val previousOpusConfig = opusConfig
                    sessionId.set(obj.optLong("sessionId", 0L))
                    packetMs = normalizePacketMs(obj.optInt("packetMs", DEFAULT_PACKET_MS))
                    val negotiatedSampleRate = rawSampleRate
                    opusConfig = parseOpusConfig(obj)
                    if (negotiatedSampleRate != serverSampleRate) {
                        serverSampleRate = negotiatedSampleRate
                        synchronized(encodeLock) {
                            codec = OpusCodecFactory().create(serverSampleRate, CHANNELS, opusConfig)
                        }
                    } else if (previousOpusConfig != opusConfig) {
                        synchronized(encodeLock) {
                            codec = OpusCodecFactory().create(serverSampleRate, CHANNELS, opusConfig)
                        }
                    }
                    if (previousSampleRate != serverSampleRate || previousPacketMs != packetMs) {
                        restartPlaybackLoop()
                    }
                    busyMode = obj.optBoolean("busyMode", false)
                    busyRxActive.set(false)
                    if (!channelBound) {
                        val selectedChannel = channel.trim()
                        if (selectedChannel.isNotBlank()) {
                            ws.send("""{"type":"join","channel":"$selectedChannel"}""")
                            channelBound = true
                            sendRepeaterModeCommand(ws)
                            sendUdpHello()
                            startUdpKeepaliveLoop()
                            enterUdpKeepaliveRecoveryWindow()
                            // Immediate UDP punch so server can start sending right away after reconnect.
                            sendUdpKeepalivePacket()
                        }
                    }
                }
                "pong" -> Unit
                "tx_stop" -> {
                    forceStopTransmissionFromServer()
                }
            }
        }
    }

    private fun forceStopTransmissionFromServer() {
        // Server-side transmit timeout requested immediate TX stop.
        val wasTx = transmitting.getAndSet(false)
        if (wasTx) {
            txJob?.cancel()
            txJob = null
            sendTxEof()
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
        scheduleRxResumeHoldoff()
        broadcastStatus(currentSignalByte())
    }

    private fun ensureUdpSocket() {
        synchronized(udpSocketLock) {
            if (udpSocket?.isClosed == false) return
            udpSocket = DatagramSocket().apply {
                reuseAddress = true
                soTimeout = 300
            }
        }
    }

    private fun recreateUdpSocket() {
        synchronized(udpSocketLock) {
            runCatching { udpSocket?.close() }
            udpSocket = null
        }
        ensureUdpSocket()
        sendUdpHello()
        enterUdpKeepaliveRecoveryWindow()
        // Also punch immediately to reduce "no UDP until next TX" on reconnects.
        sendUdpKeepalivePacket()
    }

    private fun ensurePlaybackLoop() {
        if (udpReceiveJob?.isActive == true) return
        udpReceiveJob = serviceScope.launch(Dispatchers.IO) {
            val minBuffer = AudioTrack.getMinBufferSize(
                currentCodecSampleRate(),
                AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
            )
            val track = AudioTrack(
                AudioManager.STREAM_MUSIC,
                currentCodecSampleRate(),
                AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                minBuffer.coerceAtLeast(MAX_PACKET_SAMPLES * 4),
                AudioTrack.MODE_STREAM,
            )
            track.play()
            try {
                val recvBuffer = ByteArray(1500)
                while (isActive) {
                    val socket = udpSocket ?: run {
                        ensureUdpSocket()
                        delay(120L)
                        continue
                    }
                    val packet = DatagramPacket(recvBuffer, recvBuffer.size)
                    try {
                        socket.receive(packet)
                    } catch (_: java.net.SocketTimeoutException) {
                        continue
                    } catch (_: Exception) {
                        recreateUdpSocket()
                        continue
                    }
                    if (packet.length <= 9) continue
                    if (busyMode) {
                        busyLastRxAtNs.set(System.nanoTime())
                        if (!busyRxActive.getAndSet(true)) {
                            broadcastStatus(currentSignalByte())
                        }
                    }
                    val nowNs = System.nanoTime()
                    val txActive = transmitting.get() || rogerStreaming.get() || callStreaming.get()
                    if (txActive) {
                        val lastAt = lastInboundUdpAtNs.getAndSet(nowNs)
                        val isNewBurst = lastAt == 0L || (nowNs - lastAt) > 600_000_000L // 600 ms
                        val lastBuzz = lastTxCollisionVibrateAtNs.get()
                        val cooldownOk = lastBuzz == 0L || (nowNs - lastBuzz) > 1_500_000_000L // 1.5 s
                        if (isNewBurst && cooldownOk) {
                            lastTxCollisionVibrateAtNs.set(nowNs)
                            vibrateTxCollision()
                        }
                    }
                    if (transmitting.get() || rogerStreaming.get() || callStreaming.get() || isRxHoldoffActive()) {
                        // During local TX we intentionally drop inbound audio
                        // to avoid hearing server stream in parallel with speaking.
                        continue
                    }
                    val opus = packet.data.copyOfRange(9, packet.length)
                    val pcm = applyRxVolume(codec.decode(opus, currentFrameSamples()))
                    track.write(pcm, 0, pcm.size)
                }
            } finally {
                track.stop()
                track.release()
            }
        }
    }

    private fun restartPlaybackLoop() {
        udpReceiveJob?.cancel()
        udpReceiveJob = null
        ensurePlaybackLoop()
    }

    private fun vibrateTxCollision() {
        runCatching {
            val vibrator = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                getSystemService(VibratorManager::class.java)?.defaultVibrator
            } else {
                @Suppress("DEPRECATION")
                getSystemService(VIBRATOR_SERVICE) as? Vibrator
            }
            if (vibrator == null || !vibrator.hasVibrator()) return

            // Three short pulses: on/off/on/off/on
            val pattern = longArrayOf(0, 35, 55, 35, 55, 35)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                vibrator.vibrate(VibrationEffect.createWaveform(pattern, -1))
            } else {
                @Suppress("DEPRECATION")
                vibrator.vibrate(pattern, -1)
            }
        }
    }

    private fun onPttPress() {
        rogerJob?.cancel()
        rogerJob = null
        rogerStreaming.set(false)
        callSignalJob?.cancel()
        callSignalJob = null
        callStreaming.set(false)
        localCallPlaybackJob?.cancel()
        localCallPlaybackJob = null
        if (transmitting.getAndSet(true)) return
        val micOption = microphoneConfigStore.getSelectedOption()
        ensureVoiceAudioProfile(useBluetoothHeadset || micOption.preferBluetooth)
        localPttPressPlaybackJob?.cancel()
        localPttPressPlaybackJob = serviceScope.launch(Dispatchers.IO) {
            playLocalSignalPcm(localPttPressPcm, LOCAL_PLAYBACK_SAMPLE_RATE, 0.0)
        }
        if (txJob?.isActive == true) return
        txJob = serviceScope.launch(Dispatchers.Default) {
            runCaptureLoop()
        }
    }

    private fun onPttRelease() {
        if (!transmitting.getAndSet(false)) return
        scheduleRxResumeHoldoff()
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
                sendTxEof()
            } finally {
                scheduleRxResumeHoldoff()
                rogerStreaming.set(false)
                releaseVoiceProfileIfIdle()
            }
        }
    }

    private fun onCallSignal() {
        if (transmitting.get() || rogerStreaming.get()) return
        if (callStreaming.getAndSet(true)) return
        ensureVoiceAudioProfile(useBluetoothHeadset)
        localCallPlaybackJob?.cancel()
        callSignalJob = serviceScope.launch(Dispatchers.Default) {
            try {
                val callPcm = generateCallFromSelectedPattern(currentCodecSampleRate())
                val localCallPcm = generateCallFromSelectedPattern(LOCAL_PLAYBACK_SAMPLE_RATE)
                localCallPlaybackJob = serviceScope.launch(Dispatchers.IO) {
                    playLocalSignalPcm(localCallPcm, LOCAL_PLAYBACK_SAMPLE_RATE, CALL_LOCAL_GAIN_DB)
                }
                streamGeneratedSignal(callPcm)
                sendTxEof()
            } finally {
                scheduleRxResumeHoldoff()
                callStreaming.set(false)
                releaseVoiceProfileIfIdle()
            }
        }
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
        ) ?: return@withContext
        disableRecordPreprocessing(recorder.audioSessionId)

        val readBuffer = ShortArray(frameSamples)
        val txBuffer = ShortArray(frameSamples)
        var txFill = 0
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
                        val opus = encodePcm(txBuffer)
                        val signal = currentSignalByte()
                        sendUdpFrame(opus, signal)
                        txFill = 0
                    }
                }
            }
            recorder.stop()
        } finally {
            recorder.release()
        }
    }

    private suspend fun streamRogerBeep(beepPcm: ShortArray) {
        streamGeneratedSignal(beepPcm)
    }

    private suspend fun streamGeneratedSignal(pcmSignal: ShortArray) {
        var offset = 0
        val frameSamples = currentFrameSamples()
        while (offset < pcmSignal.size) {
            val frame = ShortArray(frameSamples)
            val end = (offset + frameSamples).coerceAtMost(pcmSignal.size)
            val count = end - offset
            if (count > 0) {
                System.arraycopy(pcmSignal, offset, frame, 0, count)
            }
            val opus = encodePcm(frame)
            sendUdpFrame(opus, currentSignalByte())
            offset = end
            delay(currentPacketMs().toLong())
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
        return generateSignalFromPattern(sampleRate, selected.points, appendTail = false)
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
            val phaseStep = 2.0 * PI * seg.freqHz / sampleRate
            for (i in 0 until n) {
                val envPos = i.toDouble() / n
                val env = when {
                    envPos < 0.08 -> envPos / 0.08
                    envPos > 0.92 -> (1.0 - envPos) / 0.08
                    else -> 1.0
                }
                val sample = sin(phase) * env * 0.26
                if (idx < out.size) {
                    out[idx] = (sample * Short.MAX_VALUE).toInt().toShort()
                    idx++
                }
                phase += phaseStep
            }
        }
        return if (idx == out.size) out else out.copyOf(idx)
    }


    private fun sendUdpFrame(opusBytes: ByteArray, signalByte: Int) {
        if (opusBytes.isEmpty()) {
            return
        }
        val socket = udpSocket ?: run {
            ensureUdpSocket()
            udpSocket ?: return
        }
        val address = targetUdpAddress ?: run {
            targetUdpAddress = resolveHost(serverHost)
            targetUdpAddress ?: return
        }
        val sid = sessionId.get().toInt()
        val seqNum = seq.incrementAndGet()
        val payload = ByteArray(9 + opusBytes.size)
        val bb = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN)
        bb.putInt(sid)
        bb.putInt(seqNum)
        bb.put(signalByte.toByte())
        bb.put(opusBytes)

        val packet = DatagramPacket(payload, payload.size, address, udpPort)
        runCatching { socket.send(packet) }
            .onSuccess { lastOutboundUdpAtNs.set(System.nanoTime()) }
            .onFailure {
                recreateUdpSocket()
            }
    }

    private fun encodePcm(pcm: ShortArray): ByteArray {
        synchronized(encodeLock) {
            return codec.encode(pcm)
        }
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
                ensureUdpSocket()
                if (desiredConnection.get() && !wsConnected.get() && webSocket == null) {
                    scheduleWsReconnect()
                }
                // Re-evaluate headset availability continuously so checked mode
                // auto-applies when headset connects and auto-falls back when disconnected.
                enforceAudioRoutePolicy()
                val signal = currentSignalByte()
                updateBusyRxState()
                broadcastStatus(signal)
                delay(1000L)
            }
        }
    }

    private fun updateBusyRxState() {
        if (!busyMode) {
            busyRxActive.set(false)
            return
        }
        val holdNs = (currentPacketMs().coerceAtLeast(DEFAULT_PACKET_MS) * 2L) * 1_000_000L
        val active = (System.nanoTime() - busyLastRxAtNs.get()) <= holdNs
        busyRxActive.set(active)
    }

    private fun broadcastStatus(signal: Int) {
        updateForegroundNotification()
        val statusIntent = Intent(ACTION_STATUS).apply {
            setPackage(packageName)
            putExtra(EXTRA_SIGNAL, signal.coerceIn(0, 255))
            putExtra(EXTRA_WS_CONNECTED, wsConnected.get())
            putExtra(EXTRA_WS_CONNECTING, desiredConnection.get() && !wsConnected.get())
            putExtra(EXTRA_UDP_READY, udpSocket?.isClosed == false)
            putExtra(EXTRA_PROTOCOL_ERROR, protocolError)
            putExtra(EXTRA_BUSY_MODE, busyMode)
            putExtra(EXTRA_BUSY_RX_ACTIVE, busyRxActive.get())
            putExtra(EXTRA_TX_ACTIVE, transmitting.get() || rogerStreaming.get() || callStreaming.get())
            putExtra(EXTRA_CALL_ACTIVE, callStreaming.get())
        }
        sendBroadcast(statusIntent)
    }

    private fun scheduleWsReconnect() {
        if (wsReconnectJob?.isActive == true) return
        wsReconnectJob = serviceScope.launch {
            val attempt = wsRetryAttempt.incrementAndGet()
            val delayMs = (1000L * (1 shl (attempt - 1).coerceIn(0, 5))).coerceAtMost(30000L)
            delay(delayMs)
            if (desiredConnection.get() && !wsConnected.get()) {
                connectWebSocket(force = true)
            }
        }
    }

    private fun sendUdpHello() {
        val localPort = udpSocket?.localPort ?: return
        webSocket?.send("""{"type":"udp_hello","udpPort":$localPort}""")
    }

    private fun setRepeaterMode(enabled: Boolean) {
        repeaterEnabled = enabled
        webSocket?.let { sendRepeaterModeCommand(it) }
    }

    private fun sendRepeaterModeCommand(ws: WebSocket) {
        ws.send("""{"type":"repeater_mode","enabled":$repeaterEnabled}""")
    }

    private fun sendTxEof() {
        sendUdpEofBurst()
    }

    private fun sendUdpEofBurst() {
        serviceScope.launch(Dispatchers.IO) {
            val scheduleMs = intArrayOf(0, 20, 60)
            for (waitMs in scheduleMs) {
                if (waitMs > 0) {
                    delay(waitMs.toLong())
                }
                sendUdpEofPacket()
            }
        }
    }

    private fun sendUdpEofPacket() {
        val socket = udpSocket ?: run {
            ensureUdpSocket()
            udpSocket ?: return
        }
        val address = targetUdpAddress ?: run {
            targetUdpAddress = resolveHost(serverHost)
            targetUdpAddress ?: return
        }
        val sid = sessionId.get().toInt()
        if (sid == 0) return

        val seqNum = seq.incrementAndGet()
        val payload = ByteArray(9)
        val bb = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN)
        bb.putInt(sid)
        bb.putInt(seqNum)
        bb.put(0) // signal=0 with empty payload marks UDP TX EOF on relay
        val packet = DatagramPacket(payload, payload.size, address, udpPort)
        runCatching { socket.send(packet) }
            .onSuccess { lastOutboundUdpAtNs.set(System.nanoTime()) }
            .onFailure { recreateUdpSocket() }
    }

    private fun sendUdpKeepalivePacket() {
        val socket = udpSocket ?: run {
            ensureUdpSocket()
            udpSocket ?: return
        }
        val address = targetUdpAddress ?: run {
            targetUdpAddress = resolveHost(serverHost)
            targetUdpAddress ?: return
        }
        val sid = sessionId.get().toInt()
        if (sid == 0) return

        val payload = ByteArray(9)
        val bb = ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN)
        bb.putInt(sid)
        bb.putInt(0)
        bb.put(255.toByte())

        val packet = DatagramPacket(payload, payload.size, address, udpPort)
        runCatching { socket.send(packet) }
            .onSuccess { lastOutboundUdpAtNs.set(System.nanoTime()) }
            .onFailure { recreateUdpSocket() }
    }

    private fun startUdpKeepaliveLoop() {
        if (udpKeepaliveJob?.isActive == true) return
        udpKeepaliveJob = serviceScope.launch(Dispatchers.IO) {
            while (isActive) {
                if (!desiredConnection.get() || !wsConnected.get() || sessionId.get() == 0L) {
                    delay(1000L)
                    continue
                }
                val nowNs = System.nanoTime()
                val inRecovery = nowNs < udpKeepaliveRecoveryUntilNs.get()
                val intervalSec = if (inRecovery) UDP_KEEPALIVE_RECOVERY_INTERVAL_SEC else UDP_KEEPALIVE_IDLE_INTERVAL_SEC
                val intervalNs = intervalSec * 1_000_000_000L
                val lastTrafficNs = maxOf(lastInboundUdpAtNs.get(), lastOutboundUdpAtNs.get())
                if (lastTrafficNs == 0L || (nowNs - lastTrafficNs) >= intervalNs) {
                    sendUdpKeepalivePacket()
                }
                delay(jitteredKeepaliveDelayMs(intervalSec))
            }
        }
    }

    private fun stopUdpKeepaliveLoop() {
        udpKeepaliveJob?.cancel()
        udpKeepaliveJob = null
    }

    private fun enterUdpKeepaliveRecoveryWindow() {
        udpKeepaliveRecoveryUntilNs.set(
            System.nanoTime() + UDP_KEEPALIVE_RECOVERY_WINDOW_SEC * 1_000_000_000L,
        )
    }

    private fun jitteredKeepaliveDelayMs(baseIntervalSec: Long): Long {
        val baseMs = baseIntervalSec * 1000L
        val deltaMs = (baseMs * UDP_KEEPALIVE_JITTER_PERCENT) / 100L
        val minMs = (baseMs - deltaMs).coerceAtLeast(1000L)
        val maxMs = baseMs + deltaMs
        return if (maxMs <= minMs) minMs else Random.nextLong(minMs, maxMs + 1L)
    }

    private fun wsUrl(): String {
        val host = serverHost.trim()
        if (host.isBlank()) return ""
        val hostPart = if (host.contains(':') && !host.startsWith("[")) "[$host]" else host
        val scheme = if (wsSecure) "wss" else "ws"
        return "$scheme://$hostPart:$wsPort/ws"
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

    private fun parseOpusConfig(obj: JSONObject): OpusConfig {
        val opusObj = obj.optJSONObject("opus") ?: return OpusConfig()
        return OpusConfig(
            bitrate = normalizeOpusBitrate(opusObj.optInt("bitrate", 12000)),
            complexity = normalizeOpusComplexity(opusObj.optInt("complexity", 5)),
            fec = opusObj.optBoolean("fec", true),
            dtx = opusObj.optBoolean("dtx", false),
            application = normalizeOpusApplication(opusObj.optString("application", "voip")),
        )
    }

    private fun normalizeOpusBitrate(value: Int): Int = value.coerceIn(6000, 510000)

    private fun normalizeOpusComplexity(value: Int): Int = value.coerceIn(0, 10)

    private fun normalizeOpusApplication(value: String): String {
        return when (value.trim().lowercase()) {
            "voip", "audio", "lowdelay" -> value.trim().lowercase()
            else -> "voip"
        }
    }

    private fun currentCodecSampleRate(): Int = normalizeSampleRate(serverSampleRate)

    private fun currentPacketMs(): Int = normalizePacketMs(packetMs)

    private fun scheduleRxResumeHoldoff(multiplier: Int = 2) {
        val holdMs = (currentPacketMs() * multiplier).coerceAtLeast(currentPacketMs())
        rxResumeAtNs.set(System.nanoTime() + (holdMs * 1_000_000L))
    }

    private fun isRxHoldoffActive(): Boolean = System.nanoTime() < rxResumeAtNs.get()

    private fun currentFrameSamples(): Int = (currentCodecSampleRate() * currentPacketMs()) / 1000

    private fun resolveHost(host: String): InetAddress? {
        return try {
            InetAddress.getByName(host.removePrefix("[").removeSuffix("]"))
        } catch (_: UnknownHostException) {
            null
        }
    }

    private fun applyConfigFromIntent(intent: Intent?): Boolean {
        if (intent == null) return false
        val newHost = intent.getStringExtra(EXTRA_SERVER_HOST)?.trim().orEmpty()
        val newWsPort = intent.getIntExtra(EXTRA_WS_PORT, -1)
        val newUdpPort = intent.getIntExtra(EXTRA_UDP_PORT, -1)
        val newChannel = intent.getStringExtra(EXTRA_CHANNEL)?.trim().orEmpty()
        val newRepeaterEnabled = intent.getBooleanExtra(EXTRA_REPEATER_ENABLED, repeaterEnabled)
        val incomingRxVolume = intent.getIntExtra(EXTRA_RX_VOLUME_PERCENT, rxVolumePercent)
        val incomingUseBluetoothHeadset = intent.getBooleanExtra(EXTRA_USE_BLUETOOTH_HEADSET, useBluetoothHeadset)
        val parsedEndpoint = parseServerEndpoint(newHost, newWsPort)

        if (parsedEndpoint == null || newUdpPort !in 1..65535 || newChannel.isBlank()) {
            return false
        }

        synchronized(configLock) {
            updateRxVolumePercent(incomingRxVolume)
            useBluetoothHeadset = incomingUseBluetoothHeadset
            bluetoothHeadsetRouteStore.setEnabled(incomingUseBluetoothHeadset)
            val changed = serverHost != parsedEndpoint.host ||
                wsPort != parsedEndpoint.wsPort ||
                udpPort != newUdpPort ||
                channel != newChannel ||
                repeaterEnabled != newRepeaterEnabled ||
                wsSecure != parsedEndpoint.wsSecure
            if (!changed) return false

            serverHost = parsedEndpoint.host
            wsPort = parsedEndpoint.wsPort
            udpPort = newUdpPort
            channel = newChannel
            repeaterEnabled = newRepeaterEnabled
            wsSecure = parsedEndpoint.wsSecure
            targetUdpAddress = null
            sessionId.set(0)
            seq.set(0)
            wsConnected.set(false)
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
        enforceAudioRoutePolicy()
    }

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
        val wsPort: Int,
        val wsSecure: Boolean,
    )

    private fun parseServerEndpoint(rawHost: String, fallbackWsPort: Int): ParsedServerEndpoint? {
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

        var port = fallbackWsPort
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
        return ParsedServerEndpoint(host = host, wsPort = port, wsSecure = secure)
    }

    private fun cancelConnection() {
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
        desiredConnection.set(false)
        wsConnected.set(false)
        wsOpening.set(false)
        channelBound = false
        protocolError = false
        busyMode = false
        busyRxActive.set(false)
        busyLastRxAtNs.set(0L)
        lastInboundUdpAtNs.set(0L)
        lastOutboundUdpAtNs.set(0L)
        udpKeepaliveRecoveryUntilNs.set(0L)
        wsReconnectJob?.cancel()
        wsReconnectJob = null
        stopUdpKeepaliveLoop()
        runCatching { webSocket?.close(1000, "cancel requested") }
        runCatching { webSocket?.cancel() }
        webSocket = null
        synchronized(udpSocketLock) {
            runCatching { udpSocket?.close() }
            udpSocket = null
        }
        restoreMediaAudioProfile()
        broadcastStatus(currentSignalByte())
    }

    private fun disconnectAndStopService() {
        cancelConnection()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun exitApp() {
        transmitting.set(false)
        txJob?.cancel()
        rogerJob?.cancel()
        callSignalJob?.cancel()
        localRogerPlaybackJob?.cancel()
        localCallPlaybackJob?.cancel()
        cancelConnection()
        restoreMediaAudioProfile()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
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
        var wifiStrength = 0
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager
        if (wifiManager != null && wifiManager.isWifiEnabled) {
            val rssi = wifiManager.connectionInfo?.rssi ?: -100
            wifiStrength = WifiManager.calculateSignalLevel(rssi, 256)
        }

        var cellularStrength = 0
        val telephonyManager = getSystemService(Context.TELEPHONY_SERVICE) as? TelephonyManager
        if (telephonyManager != null) {
            runCatching {
                val level0to4 = telephonyManager.signalStrength?.level ?: 0
                cellularStrength = ((level0to4 / 4.0) * 255.0).toInt().coerceIn(0, 255)
            }
        }

        return maxOf(wifiStrength, cellularStrength).coerceIn(0, 255)
    }
}

