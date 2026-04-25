package com.owalkie.app

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
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.AutomaticGainControl
import android.media.audiofx.NoiseSuppressor
import android.net.wifi.WifiManager
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.telephony.TelephonyManager
import androidx.core.app.NotificationCompat
import com.owalkie.app.audio.OpusCodec
import com.owalkie.app.audio.OpusCodecFactory
import com.owalkie.app.model.CallingPatternStore
import com.owalkie.app.model.RogerPatternStore
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

class WalkieService : Service() {
    companion object {
        const val ACTION_START = "com.owalkie.app.action.START"
        const val ACTION_CANCEL_CONNECT = "com.owalkie.app.action.CANCEL_CONNECT"
        const val ACTION_PTT_PRESS = "com.owalkie.app.action.PTT_PRESS"
        const val ACTION_PTT_RELEASE = "com.owalkie.app.action.PTT_RELEASE"
        const val ACTION_SET_REPEATER = "com.owalkie.app.action.SET_REPEATER"
        const val ACTION_CALL_SIGNAL = "com.owalkie.app.action.CALL_SIGNAL"
        const val ACTION_EXIT_APP = "com.owalkie.app.action.EXIT_APP"
        const val ACTION_STATUS = "com.owalkie.app.action.STATUS"
        const val EXTRA_SIGNAL = "signal"
        const val EXTRA_WS_CONNECTED = "wsConnected"
        const val EXTRA_WS_CONNECTING = "wsConnecting"
        const val EXTRA_UDP_READY = "udpReady"
        const val EXTRA_SERVER_HOST = "serverHost"
        const val EXTRA_WS_PORT = "wsPort"
        const val EXTRA_UDP_PORT = "udpPort"
        const val EXTRA_CHANNEL = "channel"
        const val EXTRA_REPEATER_ENABLED = "repeaterEnabled"

        private const val NOTIFICATION_CHANNEL_ID = "owalkie_stream"
        private const val NOTIFICATION_ID = 101

        private const val DEFAULT_WS_HOST = "10.0.2.2"
        private const val DEFAULT_WS_PORT = 8080
        private const val DEFAULT_UDP_PORT = 5000
        private const val DEFAULT_CHANNEL = "global"

        private const val SAMPLE_RATE = 8000
        private const val CHANNELS = 1
        private const val DEFAULT_PACKET_MS = 20
        private const val MAX_PACKET_SAMPLES = SAMPLE_RATE * 60 / 1000
        private const val ROGER_TAIL_MS = 40
        private const val CALL_LOCAL_GAIN_DB = -10.0
    }

    private val binder = Binder()
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private val okHttpClient = OkHttpClient.Builder().retryOnConnectionFailure(true).build()
    private var webSocket: WebSocket? = null
    private var wsReconnectJob: Job? = null
    private var signalMonitorJob: Job? = null
    private val wsConnected = AtomicBoolean(false)
    private val wsRetryAttempt = AtomicInteger(0)
    private val desiredConnection = AtomicBoolean(false)
    private val configLock = Any()

    @Volatile
    private var udpSocket: DatagramSocket? = null
    private val udpSocketLock = Any()

    private var udpReceiveJob: Job? = null
    private var txJob: Job? = null
    private var rogerJob: Job? = null
    private var callSignalJob: Job? = null
    private var localRogerPlaybackJob: Job? = null
    private var localCallPlaybackJob: Job? = null

    private val transmitting = AtomicBoolean(false)
    private val rogerStreaming = AtomicBoolean(false)
    private val callStreaming = AtomicBoolean(false)
    private val sessionId = AtomicLong(0L)
    private val seq = AtomicInteger(0)
    private val encodeLock = Any()

    @Volatile
    private var serverHost: String = DEFAULT_WS_HOST

    @Volatile
    private var wsPort: Int = DEFAULT_WS_PORT

    @Volatile
    private var udpPort: Int = DEFAULT_UDP_PORT

    @Volatile
    private var channel: String = DEFAULT_CHANNEL

    @Volatile
    private var targetUdpAddress: InetAddress? = resolveHost(DEFAULT_WS_HOST)

    @Volatile
    private var repeaterEnabled: Boolean = false

    @Volatile
    private var packetMs: Int = DEFAULT_PACKET_MS

    private lateinit var codec: OpusCodec
    private lateinit var audioManager: AudioManager
    private lateinit var rogerPatternStore: RogerPatternStore
    private lateinit var callingPatternStore: CallingPatternStore
    @Volatile
    private var voiceProfileActive: Boolean = false

    override fun onCreate() {
        super.onCreate()
        codec = OpusCodecFactory().create(SAMPLE_RATE, CHANNELS)
        audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        rogerPatternStore = RogerPatternStore(this)
        callingPatternStore = CallingPatternStore(this)
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> {
                desiredConnection.set(true)
                startCore(intent)
            }
            ACTION_CANCEL_CONNECT -> cancelConnection()
            ACTION_PTT_PRESS -> onPttPress()
            ACTION_PTT_RELEASE -> onPttRelease()
            ACTION_SET_REPEATER -> setRepeaterMode(intent.getBooleanExtra(EXTRA_REPEATER_ENABLED, false))
            ACTION_CALL_SIGNAL -> onCallSignal()
            ACTION_EXIT_APP -> exitApp()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()
        transmitting.set(false)
        txJob?.cancel()
        rogerJob?.cancel()
        callSignalJob?.cancel()
        localRogerPlaybackJob?.cancel()
        localCallPlaybackJob?.cancel()
        udpReceiveJob?.cancel()
        wsReconnectJob?.cancel()
        signalMonitorJob?.cancel()
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
        startForegroundInternal()
        ensureUdpSocket()
        if (changed) {
            connectWebSocket(force = true)
        } else {
            connectWebSocket()
        }
        ensurePlaybackLoop()
        ensureSignalMonitorLoop()
    }

    private fun startForegroundInternal() {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                NOTIFICATION_CHANNEL_ID,
                getString(R.string.service_channel_name),
                NotificationManager.IMPORTANCE_LOW,
            )
            nm.createNotificationChannel(channel)
        }
        val notification: Notification = NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle(getString(R.string.service_notification_title))
            .setContentText(getString(R.string.service_notification_text))
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setOngoing(true)
            .setContentIntent(mainActivityPendingIntent())
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

    private fun connectWebSocket(force: Boolean = false) {
        if (!desiredConnection.get()) return
        if (!force && webSocket != null) return
        if (force) {
            runCatching { webSocket?.cancel() }
            webSocket = null
        }
        val request = Request.Builder().url(wsUrl()).build()
        webSocket = okHttpClient.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                wsConnected.set(true)
                wsRetryAttempt.set(0)
                packetMs = DEFAULT_PACKET_MS
                webSocket.send("""{"type":"join","channel":"$channel"}""")
                sendRepeaterModeCommand(webSocket)
                sendUdpHello()
                broadcastStatus(currentSignalByte())
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                handleWsMessage(text)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                wsConnected.set(false)
                this@WalkieService.webSocket = null
                if (desiredConnection.get()) {
                    scheduleWsReconnect()
                } else {
                    broadcastStatus(currentSignalByte())
                }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                wsConnected.set(false)
                this@WalkieService.webSocket = null
                if (desiredConnection.get()) {
                    scheduleWsReconnect()
                } else {
                    broadcastStatus(currentSignalByte())
                }
            }
        })
    }

    private fun handleWsMessage(text: String) {
        runCatching {
            val obj = JSONObject(text)
            when (obj.optString("type")) {
                "welcome" -> {
                    sessionId.set(obj.optLong("sessionId", 0L))
                    packetMs = normalizePacketMs(obj.optInt("packetMs", DEFAULT_PACKET_MS))
                }
                "pong" -> Unit
            }
        }
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
    }

    private fun ensurePlaybackLoop() {
        if (udpReceiveJob?.isActive == true) return
        udpReceiveJob = serviceScope.launch(Dispatchers.IO) {
            val minBuffer = AudioTrack.getMinBufferSize(
                SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
            )
            val track = AudioTrack(
                AudioManager.STREAM_MUSIC,
                SAMPLE_RATE,
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
                    if (transmitting.get() || rogerStreaming.get() || callStreaming.get()) {
                        // During local TX we intentionally drop inbound audio
                        // to avoid hearing server stream in parallel with speaking.
                        continue
                    }
                    val opus = packet.data.copyOfRange(9, packet.length)
                    val pcm = codec.decode(opus, currentFrameSamples())
                    track.write(pcm, 0, pcm.size)
                }
            } finally {
                track.stop()
                track.release()
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
        ensureVoiceAudioProfile()
        if (txJob?.isActive == true) return
        txJob = serviceScope.launch(Dispatchers.Default) {
            runCaptureLoop()
        }
    }

    private fun onPttRelease() {
        if (!transmitting.getAndSet(false)) return
        if (rogerStreaming.getAndSet(true)) return
        localRogerPlaybackJob?.cancel()
        rogerJob = serviceScope.launch(Dispatchers.Default) {
            try {
                val rogerPcm = generateRogerFromSelectedPattern(SAMPLE_RATE)
                localRogerPlaybackJob = serviceScope.launch(Dispatchers.IO) {
                    playLocalRogerPcm(rogerPcm)
                }
                streamRogerBeep(rogerPcm)
            } finally {
                rogerStreaming.set(false)
            }
        }
    }

    private fun onCallSignal() {
        if (transmitting.get() || rogerStreaming.get()) return
        if (callStreaming.getAndSet(true)) return
        ensureVoiceAudioProfile()
        localCallPlaybackJob?.cancel()
        callSignalJob = serviceScope.launch(Dispatchers.Default) {
            try {
                val callPcm = generateCallFromSelectedPattern(SAMPLE_RATE)
                localCallPlaybackJob = serviceScope.launch(Dispatchers.IO) {
                    playLocalSignalPcm(callPcm, CALL_LOCAL_GAIN_DB)
                }
                streamGeneratedSignal(callPcm)
            } finally {
                callStreaming.set(false)
            }
        }
    }

    private suspend fun runCaptureLoop() = withContext(Dispatchers.IO) {
        val frameSamples = currentFrameSamples()
        val minBuffer = AudioRecord.getMinBufferSize(
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        val recorder = AudioRecord(
            MediaRecorder.AudioSource.MIC,
            SAMPLE_RATE,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
            minBuffer.coerceAtLeast(frameSamples * 4),
        )
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

    private suspend fun playLocalRogerPcm(pcm: ShortArray) = withContext(Dispatchers.IO) {
        playLocalSignalPcm(pcm, 0.0)
    }

    private suspend fun playLocalSignalPcm(pcm: ShortArray, gainDb: Double) = withContext(Dispatchers.IO) {
        if (pcm.isEmpty()) return@withContext
        val playPcm = applyGainDb(pcm, gainDb)
        val track = AudioTrack(
            AudioManager.STREAM_MUSIC,
            SAMPLE_RATE,
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
            track.stop()
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

    private fun generateRogerFromSelectedPattern(sampleRate: Int): ShortArray {
        val selected = rogerPatternStore.getSelectedPattern()
        return generateSignalFromPattern(sampleRate, selected.points, appendTail = true)
    }

    private fun generateCallFromSelectedPattern(sampleRate: Int): ShortArray {
        val selected = callingPatternStore.getSelectedPattern()
        return generateSignalFromPattern(sampleRate, selected.points, appendTail = false)
    }

    private fun generateSignalFromPattern(
        sampleRate: Int,
        segments: List<com.owalkie.app.model.RogerPoint>,
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
                val signal = currentSignalByte()
                broadcastStatus(signal)
                delay(1000L)
            }
        }
    }

    private fun broadcastStatus(signal: Int) {
        val statusIntent = Intent(ACTION_STATUS).apply {
            setPackage(packageName)
            putExtra(EXTRA_SIGNAL, signal.coerceIn(0, 255))
            putExtra(EXTRA_WS_CONNECTED, wsConnected.get())
            putExtra(EXTRA_WS_CONNECTING, desiredConnection.get() && !wsConnected.get())
            putExtra(EXTRA_UDP_READY, udpSocket?.isClosed == false)
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

    private fun wsUrl(): String = "ws://$serverHost:$wsPort/ws"

    private fun normalizePacketMs(value: Int): Int {
        return when (value) {
            10, 20, 40, 60 -> value
            else -> DEFAULT_PACKET_MS
        }
    }

    private fun currentPacketMs(): Int = normalizePacketMs(packetMs)

    private fun currentFrameSamples(): Int = (SAMPLE_RATE * currentPacketMs()) / 1000

    private fun resolveHost(host: String): InetAddress? {
        return try {
            InetAddress.getByName(host)
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

        if (newHost.isBlank() || newWsPort !in 1..65535 || newUdpPort !in 1..65535 || newChannel.isBlank()) {
            return false
        }

        synchronized(configLock) {
            val changed = serverHost != newHost ||
                wsPort != newWsPort ||
                udpPort != newUdpPort ||
                channel != newChannel ||
                repeaterEnabled != newRepeaterEnabled
            if (!changed) return false

            serverHost = newHost
            wsPort = newWsPort
            udpPort = newUdpPort
            channel = newChannel
            repeaterEnabled = newRepeaterEnabled
            targetUdpAddress = resolveHost(newHost)
            sessionId.set(0)
            seq.set(0)
            wsConnected.set(false)
            return true
        }
    }

    private fun cancelConnection() {
        desiredConnection.set(false)
        wsConnected.set(false)
        wsReconnectJob?.cancel()
        wsReconnectJob = null
        runCatching { webSocket?.close(1000, "cancel requested") }
        runCatching { webSocket?.cancel() }
        webSocket = null
        broadcastStatus(currentSignalByte())
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

    private fun ensureVoiceAudioProfile() {
        if (voiceProfileActive) return
        runCatching {
            // Test mode: use camera-like multimedia profile for recording.
            audioManager.mode = AudioManager.MODE_NORMAL
            audioManager.isSpeakerphoneOn = false
            if (audioManager.isBluetoothScoOn) {
                audioManager.stopBluetoothSco()
                audioManager.isBluetoothScoOn = false
            }
            voiceProfileActive = true
        }
    }

    private fun restoreMediaAudioProfile() {
        runCatching {
            if (audioManager.isBluetoothScoOn) {
                audioManager.stopBluetoothSco()
                audioManager.isBluetoothScoOn = false
            }
            audioManager.mode = AudioManager.MODE_NORMAL
            audioManager.isSpeakerphoneOn = false
            voiceProfileActive = false
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

