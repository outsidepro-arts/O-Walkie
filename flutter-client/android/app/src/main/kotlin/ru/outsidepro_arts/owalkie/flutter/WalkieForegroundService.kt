package ru.outsidepro_arts.owalkie.flutter

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.view.KeyEvent
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat

/**
 * Keeps the Flutter process alive while a relay session is desired (connecting or connected).
 * Audio capture/playback stays in owalkie-core FFI; this service provides FGS, notification,
 * media-button PTT capture, and hardware PTT routing while the session is active.
 */
class WalkieForegroundService : Service() {
    private var mediaPttActive = false
    private var foregroundActive = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == Intent.ACTION_MEDIA_BUTTON) {
            if (mediaPttActive) {
                PttMediaSessionHost.sync(this, true)
                PttMediaSessionHost.dispatchMediaButtonIntent(intent)
            }
            return START_STICKY
        }
        when (intent?.action) {
            ExternalControlReceiver.ACTION_PTT_DOWN -> handleExternalControl(
                PlatformEvents.EVENT_EXTERNAL_PTT_DOWN,
            )
            ExternalControlReceiver.ACTION_PTT_UP -> handleExternalControl(
                PlatformEvents.EVENT_EXTERNAL_PTT_UP,
            )
            ExternalControlReceiver.ACTION_PTT_TOGGLE -> handleExternalControl(
                PlatformEvents.EVENT_EXTERNAL_PTT_TOGGLE,
            )
            ExternalControlReceiver.ACTION_CALL_SIGNAL -> handleExternalControl(
                PlatformEvents.EVENT_EXTERNAL_CALL_SIGNAL,
            )
            ExternalControlReceiver.ACTION_CONNECT -> handleExternalControl(
                PlatformEvents.EVENT_EXTERNAL_CONNECT,
            )
            ExternalControlReceiver.ACTION_DISCONNECT -> handleExternalControl(
                PlatformEvents.EVENT_EXTERNAL_DISCONNECT,
            )
            ExternalControlReceiver.ACTION_NEXT_CONNECTION -> handleExternalControl(
                PlatformEvents.EVENT_EXTERNAL_NEXT_CONNECTION,
            )
            ExternalControlReceiver.ACTION_PREVIOUS_CONNECTION -> handleExternalControl(
                PlatformEvents.EVENT_EXTERNAL_PREVIOUS_CONNECTION,
            )
            ACTION_START -> {
                val connected = intent.getBooleanExtra(EXTRA_CONNECTED, false)
                startForegroundInternal(connected)
            }
            ACTION_UPDATE -> {
                val connected = intent.getBooleanExtra(EXTRA_CONNECTED, false)
                updateNotification(connected)
            }
            ACTION_SYNC_PTT_MEDIA_SESSION -> {
                mediaPttActive = intent.getBooleanExtra(EXTRA_MEDIA_PTT_ACTIVE, false)
                PttMediaSessionHost.sync(this, mediaPttActive)
            }
            ACTION_HARDWARE_PTT_KEY -> {
                val action = intent.getIntExtra(EXTRA_HW_KEY_ACTION, KeyEvent.ACTION_DOWN)
                val repeat = intent.getIntExtra(EXTRA_HW_KEY_REPEAT, 0)
                val keyCode = intent.getIntExtra(EXTRA_HW_KEY_CODE, KeyEvent.KEYCODE_UNKNOWN)
                val scanCode = intent.getIntExtra(EXTRA_HW_SCAN_CODE, 0)
                val event = KeyEvent(0L, 0L, action, keyCode, repeat, 0, 0, scanCode)
                HardwarePttKeyHandler.tryHandlePtt(this, event)
            }
            ACTION_NOTIFICATION_DISCONNECT -> {
                PlatformEvents.emit(PlatformEvents.EVENT_NOTIFICATION_DISCONNECT)
            }
            ACTION_STOP -> stopForegroundService()
        }
        return START_STICKY
    }

    private fun handleExternalControl(event: String) {
        ensureForegroundRunning(connected = false)
        PlatformEvents.emit(event)
    }

    private fun ensureForegroundRunning(connected: Boolean) {
        if (foregroundActive) {
            return
        }
        startForegroundInternal(connected)
    }

    private fun startForegroundInternal(connected: Boolean) {
        ensureNotificationChannel()
        val notification = buildNotification(connected)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE,
            )
        } else {
            @Suppress("DEPRECATION")
            startForeground(NOTIFICATION_ID, notification)
        }
        foregroundActive = true
    }

    private fun updateNotification(connected: Boolean) {
        ensureNotificationChannel()
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        nm.notify(NOTIFICATION_ID, buildNotification(connected))
    }

    private fun buildNotification(connected: Boolean): Notification {
        val actionLabel = if (connected) {
            getString(R.string.service_notification_action_disconnect)
        } else {
            getString(R.string.service_notification_action_connect)
        }
        return NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setContentTitle(getString(R.string.service_notification_title))
            .setContentText(getString(R.string.service_notification_text))
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setOngoing(true)
            .setContentIntent(mainActivityPendingIntent())
            .addAction(0, actionLabel, toggleConnectionPendingIntent())
            .addAction(
                0,
                getString(R.string.service_notification_action_battery),
                batterySettingsPendingIntent(),
            )
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
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
        val intent = Intent(this, WalkieForegroundService::class.java).apply {
            action = ACTION_NOTIFICATION_DISCONNECT
        }
        val flags = PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        return PendingIntent.getService(this, 2003, intent, flags)
    }

    private fun stopForegroundService() {
        mediaPttActive = false
        foregroundActive = false
        PttMediaSessionHost.release()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            stopForeground(STOP_FOREGROUND_REMOVE)
        } else {
            @Suppress("DEPRECATION")
            stopForeground(true)
        }
        stopSelf()
    }

    companion object {
        const val ACTION_START = "ru.outsidepro_arts.owalkie.flutter.action.FGS_START"
        const val ACTION_UPDATE = "ru.outsidepro_arts.owalkie.flutter.action.FGS_UPDATE"
        const val ACTION_STOP = "ru.outsidepro_arts.owalkie.flutter.action.FGS_STOP"
        const val ACTION_NOTIFICATION_DISCONNECT =
            "ru.outsidepro_arts.owalkie.flutter.action.NOTIFICATION_DISCONNECT"
        const val ACTION_SYNC_PTT_MEDIA_SESSION =
            "ru.outsidepro_arts.owalkie.flutter.action.SYNC_PTT_MEDIA_SESSION"
        const val ACTION_HARDWARE_PTT_KEY =
            "ru.outsidepro_arts.owalkie.flutter.action.HARDWARE_PTT_KEY"
        const val EXTRA_CONNECTED = "connected"
        const val EXTRA_MEDIA_PTT_ACTIVE = "media_ptt_active"
        const val EXTRA_HW_KEY_ACTION = "hw_key_action"
        const val EXTRA_HW_KEY_REPEAT = "hw_key_repeat"
        const val EXTRA_HW_KEY_CODE = "hw_key_code"
        const val EXTRA_HW_SCAN_CODE = "hw_scan_code"

        private const val NOTIFICATION_ID = 42
        private const val NOTIFICATION_CHANNEL_ID = "owalkie_session"

        fun start(context: Context, connected: Boolean) {
            val intent = Intent(context, WalkieForegroundService::class.java).apply {
                action = ACTION_START
                putExtra(EXTRA_CONNECTED, connected)
            }
            ContextCompat.startForegroundService(context, intent)
        }

        fun update(context: Context, connected: Boolean) {
            val intent = Intent(context, WalkieForegroundService::class.java).apply {
                action = ACTION_UPDATE
                putExtra(EXTRA_CONNECTED, connected)
            }
            context.startService(intent)
        }

        fun stop(context: Context) {
            val intent = Intent(context, WalkieForegroundService::class.java).apply {
                action = ACTION_STOP
            }
            context.startService(intent)
        }

        fun syncPttMediaSession(context: Context, active: Boolean) {
            val intent = Intent(context, WalkieForegroundService::class.java).apply {
                action = ACTION_SYNC_PTT_MEDIA_SESSION
                putExtra(EXTRA_MEDIA_PTT_ACTIVE, active)
            }
            context.startService(intent)
        }

        fun forwardHardwarePttKey(context: Context, event: KeyEvent) {
            val intent = Intent(context, WalkieForegroundService::class.java).apply {
                action = ACTION_HARDWARE_PTT_KEY
                putExtra(EXTRA_HW_KEY_ACTION, event.action)
                putExtra(EXTRA_HW_KEY_REPEAT, event.repeatCount)
                putExtra(EXTRA_HW_KEY_CODE, event.keyCode)
                putExtra(EXTRA_HW_SCAN_CODE, event.scanCode)
            }
            context.startService(intent)
        }
    }
}
