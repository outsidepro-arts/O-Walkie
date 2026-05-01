package ru.outsidepro_arts.owalkie

import android.os.SystemClock
import java.util.ArrayDeque

/**
 * Limits how often a new PTT press may start real transmit (hold/toggle/hardware).
 * Sliding window — fixed anti-spam policy (not user-configurable).
 */
object PttRateLimiter {
    private const val WINDOW_MS = 600L
    private const val MAX_ACCEPTED_PRESSES = 3

    private val timesMs = ArrayDeque<Long>()

    /** @return false when this press must be ignored (spam guard). */
    fun tryAcquire(): Boolean {
        val now = SystemClock.elapsedRealtime()
        synchronized(timesMs) {
            while (timesMs.isNotEmpty() && now - timesMs.first >= WINDOW_MS) {
                timesMs.removeFirst()
            }
            if (timesMs.size >= MAX_ACCEPTED_PRESSES) return false
            timesMs.addLast(now)
            return true
        }
    }
}
