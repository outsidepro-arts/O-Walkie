package ru.outsidepro_arts.owalkie

import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import ru.outsidepro_arts.owalkie.model.RogerPoint
import kotlin.concurrent.thread
import kotlin.math.PI
import kotlin.math.sin

object SignalPreviewPlayer {
    private const val SAMPLE_RATE = 44100

    fun playPattern(points: List<RogerPoint>) {
        val pcm = generatePcm(points)
        if (pcm.isEmpty()) {
            return
        }
        thread(start = true, name = "signal-preview-player") {
            val track = AudioTrack(
                AudioManager.STREAM_MUSIC,
                SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                (pcm.size * 2).coerceAtLeast(2),
                AudioTrack.MODE_STATIC,
            )
            try {
                val written = track.write(pcm, 0, pcm.size)
                if (written <= 0) return@thread
                track.play()
                val deadline = System.currentTimeMillis() + 4000L
                while (System.currentTimeMillis() < deadline) {
                    if (track.playbackHeadPosition >= written) break
                    Thread.sleep(5L)
                }
            } catch (_: Exception) {
            } finally {
                runCatching { track.stop() }
                track.release()
            }
        }
    }

    private fun generatePcm(points: List<RogerPoint>): ShortArray {
        if (points.isEmpty()) return shortArrayOf()
        val total = points.sumOf { (SAMPLE_RATE * it.durationMs) / 1000 }.coerceAtLeast(1)
        val out = ShortArray(total)
        var idx = 0
        var phase = 0.0
        for (point in points) {
            val n = ((SAMPLE_RATE * point.durationMs) / 1000).coerceAtLeast(1)
            val isPause = point.freqHz <= 0.0
            val phaseStep = if (isPause) 0.0 else 2.0 * PI * point.freqHz / SAMPLE_RATE
            for (i in 0 until n) {
                if (idx >= out.size) break
                val envPos = i.toDouble() / n
                val env = when {
                    envPos < 0.08 -> envPos / 0.08
                    envPos > 0.92 -> (1.0 - envPos) / 0.08
                    else -> 1.0
                }
                val sample = if (isPause) 0.0 else sin(phase) * env * 0.26
                out[idx++] = (sample * Short.MAX_VALUE).toInt().toShort()
                phase += phaseStep
            }
        }
        return if (idx == out.size) out else out.copyOf(idx)
    }
}
