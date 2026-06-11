package ru.outsidepro_arts.owalkie.flutter

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.support.v4.media.session.MediaSessionCompat
import android.support.v4.media.session.PlaybackStateCompat
import android.view.KeyEvent
import androidx.media.session.MediaButtonReceiver
import java.util.concurrent.atomic.AtomicLong

/**
 * Routes headset / media play-pause events to PTT latch toggle (Flutter policy).
 */
class PttMediaSessionController(
    context: Context,
    private val mainHandler: Handler,
    private val onMediaButtonToggle: Runnable,
) {
    private val appContext = context.applicationContext
    private var session: MediaSessionCompat? = null
    private val lastToggleNs = AtomicLong(0L)

    fun release() {
        session?.release()
        session = null
    }

    fun ensureActiveAndRefreshState() {
        val s = session ?: createSession().also { session = it }
        s.isActive = true
        refreshPlaybackState(s)
    }

    fun dispatchMediaButtonIntent(intent: Intent): Boolean {
        val s = session ?: return false
        MediaButtonReceiver.handleIntent(s, intent)
        return true
    }

    private fun createSession(): MediaSessionCompat {
        val receiver = ComponentName(appContext, MediaButtonReceiver::class.java)
        val s = MediaSessionCompat(appContext, SESSION_TAG, receiver, null)
        s.setFlags(
            MediaSessionCompat.FLAG_HANDLES_MEDIA_BUTTONS or
                MediaSessionCompat.FLAG_HANDLES_TRANSPORT_CONTROLS,
        )
        s.setCallback(
            object : MediaSessionCompat.Callback() {
                override fun onPlay() = dispatchToggle()

                override fun onPause() = dispatchToggle()

                override fun onMediaButtonEvent(mediaButtonIntent: Intent?): Boolean {
                    if (mediaButtonIntent == null) {
                        return false
                    }
                    @Suppress("DEPRECATION")
                    val keyEvent =
                        mediaButtonIntent.getParcelableExtra<KeyEvent>(Intent.EXTRA_KEY_EVENT)
                            ?: return super.onMediaButtonEvent(mediaButtonIntent)
                    if (keyEvent.action != KeyEvent.ACTION_DOWN) {
                        return true
                    }
                    when (keyEvent.keyCode) {
                        KeyEvent.KEYCODE_MEDIA_PLAY,
                        KeyEvent.KEYCODE_MEDIA_PAUSE,
                        KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
                        KeyEvent.KEYCODE_HEADSETHOOK,
                        -> {
                            dispatchToggle()
                            return true
                        }
                    }
                    return super.onMediaButtonEvent(mediaButtonIntent)
                }
            },
            mainHandler,
        )
        return s
    }

    private fun refreshPlaybackState(s: MediaSessionCompat) {
        val state = PlaybackStateCompat.Builder()
            .setActions(
                PlaybackStateCompat.ACTION_PLAY or
                    PlaybackStateCompat.ACTION_PAUSE or
                    PlaybackStateCompat.ACTION_PLAY_PAUSE,
            )
            .setState(PlaybackStateCompat.STATE_PAUSED, 0L, 0f)
            .build()
        s.setPlaybackState(state)
    }

    private fun dispatchToggle() {
        val now = System.nanoTime()
        val prev = lastToggleNs.get()
        if (now - prev < DEBOUNCE_NS) {
            return
        }
        if (!lastToggleNs.compareAndSet(prev, now)) {
            return
        }
        mainHandler.post(onMediaButtonToggle)
    }

    companion object {
        private const val SESSION_TAG = "OWalkieFlutterPtt"
        private const val DEBOUNCE_NS = 400_000_000L
    }
}
