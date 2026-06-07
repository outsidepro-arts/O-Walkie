package ru.outsidepro_arts.owalkie

import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Serializes inbound RX PCM and plays at a steady frame pace.
 * Avoids per-packet coroutines and direct [AudioTrack] writes from the native callback thread.
 */
class RxPcmJitterBuffer(
    private val sampleRateProvider: () -> Int,
    private val frameSamplesProvider: () -> Int,
    private val playbackBufferFrames: Int = 6,
    private val ringFrameCapacity: Int = 48,
) {
    fun interface Listener {
        fun onPlaybackWrite(nowNs: Long, gapMsSinceLastWrite: Long)
    }

    private val lock = Any()
    private var ring = ShortArray(0)
    private var ringCapacity = 0
    private var writePos = 0
    private var readPos = 0
    private var size = 0
    private var track: AudioTrack? = null
    private val running = AtomicBoolean(false)
    private var playbackThread: Thread? = null

    @Volatile
    private var listener: Listener? = null

    fun setListener(listener: Listener?) {
        this.listener = listener
    }

    fun release() {
        running.set(false)
        playbackThread?.interrupt()
        playbackThread?.join(500)
        playbackThread = null
        synchronized(lock) {
            releaseTrackLocked()
            size = 0
            writePos = 0
            readPos = 0
            ringCapacity = 0
            ring = ShortArray(0)
        }
    }

    fun resetCodec() {
        synchronized(lock) {
            releaseTrackLocked()
            size = 0
            writePos = 0
            readPos = 0
            ringCapacity = 0
            ring = ShortArray(0)
        }
    }

    fun offer(pcm: ShortArray) {
        if (pcm.isEmpty()) {
            return
        }
        synchronized(lock) {
            ensureRingCapacityLocked()
            for (sample in pcm) {
                if (size == ringCapacity) {
                    readPos = (readPos + 1) % ringCapacity
                    size--
                }
                ring[writePos] = sample
                writePos = (writePos + 1) % ringCapacity
                size++
            }
        }
        ensurePlaybackThread()
    }

    private fun ensureRingCapacityLocked() {
        val frame = frameSamplesProvider().coerceAtLeast(1)
        val needed = (frame * ringFrameCapacity).coerceAtLeast(frame * 4)
        if (ringCapacity >= needed && ring.isNotEmpty()) {
            return
        }
        val oldRing = ring
        val oldCap = ringCapacity
        val oldSize = size
        val oldRead = readPos
        ring = ShortArray(needed)
        ringCapacity = needed
        readPos = 0
        writePos = 0
        size = 0
        if (oldCap > 0 && oldSize > 0) {
            var rp = oldRead
            repeat(oldSize.coerceAtMost(needed)) {
                ring[writePos] = oldRing[rp]
                writePos = (writePos + 1) % ringCapacity
                size++
                rp = (rp + 1) % oldCap
            }
        }
    }

    private fun ensurePlaybackThread() {
        if (running.get()) {
            return
        }
        if (!running.compareAndSet(false, true)) {
            return
        }
        val thread = Thread({ playbackLoop() }, "owalkie-rx-playback")
        thread.isDaemon = true
        thread.priority = Thread.MAX_PRIORITY - 1
        playbackThread = thread
        thread.start()
    }

    private fun playbackLoop() {
        var lastWriteNs = 0L
        var nextFrameAtNs = 0L
        while (running.get()) {
            val frameSamples = frameSamplesProvider().coerceAtLeast(1)
            val frameNs = (frameSamples * 1_000_000_000L) /
                sampleRateProvider().coerceAtLeast(1).toLong()
            val frame = ShortArray(frameSamples)
            val got = synchronized(lock) {
                if (size < frameSamples) {
                    0
                } else {
                    for (i in 0 until frameSamples) {
                        frame[i] = ring[readPos]
                        readPos = (readPos + 1) % ringCapacity
                    }
                    size -= frameSamples
                    frameSamples
                }
            }
            if (got <= 0) {
                nextFrameAtNs = 0L
                Thread.sleep(5)
                continue
            }

            val nowBeforeWrite = System.nanoTime()
            if (nextFrameAtNs == 0L) {
                nextFrameAtNs = nowBeforeWrite
            }
            val sleepNs = nextFrameAtNs - nowBeforeWrite
            if (sleepNs > 0L) {
                Thread.sleep((sleepNs / 1_000_000L).coerceAtLeast(1L))
            } else if (sleepNs < -frameNs * 2) {
                nextFrameAtNs = System.nanoTime()
            }

            synchronized(lock) {
                val trackRef = ensureTrackLocked() ?: return@synchronized
                trackRef.write(frame, 0, got)
            }

            nextFrameAtNs += frameNs
            val nowNs = System.nanoTime()
            val gapMs = if (lastWriteNs == 0L) Long.MAX_VALUE else (nowNs - lastWriteNs) / 1_000_000L
            lastWriteNs = nowNs
            listener?.onPlaybackWrite(nowNs, gapMs)
        }
    }

    private fun ensureTrackLocked(): AudioTrack? {
        val existing = track
        if (existing != null && existing.state == AudioTrack.STATE_INITIALIZED) {
            return existing
        }
        releaseTrackLocked()
        val sampleRate = sampleRateProvider()
        val minBuffer = AudioTrack.getMinBufferSize(
            sampleRate,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        val frameBytes = frameSamplesProvider().coerceAtLeast(1) * 2
        val targetBuffer = (frameBytes * playbackBufferFrames).coerceAtLeast(frameBytes)
        val playbackBufferBytes = if (minBuffer > 0) {
            minBuffer.coerceAtLeast(targetBuffer)
        } else {
            targetBuffer
        }
        val created = AudioTrack(
            AudioManager.STREAM_MUSIC,
            sampleRate,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
            playbackBufferBytes,
            AudioTrack.MODE_STREAM,
        )
        created.play()
        track = created
        return created
    }

    private fun releaseTrackLocked() {
        runCatching {
            track?.stop()
            track?.release()
        }
        track = null
    }
}
