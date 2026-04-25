package com.owalkie.app

import android.content.Context
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.sin

class UiSignalPlayer(context: Context) {
    private val appContext = context.applicationContext
    private val pttPressTone: WavPcm = loadWavPcmFromRaw(R.raw.selfpttup_002)
    private val pttReleaseTone: WavPcm = loadWavPcmFromRaw(R.raw.selfttdown_002)

    fun playPttPress() {
        playPcm(pttPressTone.samples, pttPressTone.sampleRate)
    }

    fun playPttRelease() {
        playPcm(pttReleaseTone.samples, pttReleaseTone.sampleRate)
    }

    fun playConnected() {
        val pcm = generateConnectedPcm()
        playPcm(pcm, SAMPLE_RATE)
    }

    fun playConnectionError() {
        val pcm = generateConnectionErrorPcm()
        playPcm(pcm, SAMPLE_RATE)
    }

    fun release() {
        // no-op for now, kept for lifecycle symmetry.
    }

    private fun playPcm(pcm: ShortArray, sampleRate: Int) {
        if (pcm.isEmpty()) return
        val track = AudioTrack(
            AudioManager.STREAM_MUSIC,
            sampleRate,
            AudioFormat.CHANNEL_OUT_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
            (pcm.size * 2).coerceAtLeast(2),
            AudioTrack.MODE_STATIC,
        )
        val written = track.write(pcm, 0, pcm.size)
        if (written > 0) {
            track.play()
        }
        // Release on a lightweight worker after expected playback time.
        Thread {
            try {
                Thread.sleep(((pcm.size * 1000L) / SAMPLE_RATE) + 40L)
            } catch (_: InterruptedException) {
            }
            runCatching {
                track.stop()
            }
            track.release()
        }.start()
    }

    private fun generateConnectedPcm(): ShortArray {
        val segments = listOf(
            Segment(620.0, 56),
            Segment(1330.0, 12),
            Segment(2670.0, 140),
        )
        return synthesizeSegments(segments, gain = 0.22)
    }

    private fun generateConnectionErrorPcm(): ShortArray {
        val segments = listOf(
            Segment(890.0, 192),
            Segment(0.0, 300),
            Segment(890.0, 200),
        )
        return synthesizeSegments(segments, gain = 0.2)
    }

    private fun synthesizeSegments(segments: List<Segment>, gain: Double): ShortArray {
        val total = segments.sumOf { SAMPLE_RATE * it.durationMs / 1000 }
        val out = ShortArray(total.coerceAtLeast(1))
        var idx = 0
        var phase = 0.0
        for (seg in segments) {
            val n = ((SAMPLE_RATE * seg.durationMs) / 1000).coerceAtLeast(1)
            val step = if (seg.freqHz > 0.0) (2.0 * PI * seg.freqHz / SAMPLE_RATE) else 0.0
            for (i in 0 until n) {
                val envPos = i.toDouble() / n
                val env = when {
                    envPos < 0.08 -> envPos / 0.08
                    envPos > 0.92 -> (1.0 - envPos) / 0.08
                    else -> 1.0
                }
                val sample = if (seg.freqHz > 0.0) sin(phase) * env * gain else 0.0
                if (idx < out.size) {
                    out[idx] = (sample * Short.MAX_VALUE).toInt().toShort()
                    idx++
                }
                phase += step
            }
        }
        return if (idx == out.size) out else out.copyOf(idx)
    }

    private fun loadWavPcmFromRaw(resourceId: Int): WavPcm {
        return runCatching {
            appContext.resources.openRawResource(resourceId).use { input ->
                val bytes = input.readBytes()
                if (bytes.size < 44) return WavPcm(shortArrayOf(), SAMPLE_RATE)
                if (String(bytes, 0, 4) != "RIFF" || String(bytes, 8, 4) != "WAVE") {
                    return WavPcm(shortArrayOf(), SAMPLE_RATE)
                }
                var offset = 12
                var srcRate = SAMPLE_RATE
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
                    return WavPcm(shortArrayOf(), SAMPLE_RATE)
                }
                val sampleCount = dataSize / 2
                val raw = ShortArray(sampleCount)
                var p = dataStart
                for (i in 0 until sampleCount) {
                    raw[i] = ByteBuffer.wrap(bytes, p, 2).order(ByteOrder.LITTLE_ENDIAN).short
                    p += 2
                }
                val safeRate = srcRate.takeIf { it in 4000..192000 } ?: SAMPLE_RATE
                WavPcm(raw, safeRate)
            }
        }.getOrDefault(WavPcm(shortArrayOf(), SAMPLE_RATE))
    }

    private data class WavPcm(val samples: ShortArray, val sampleRate: Int)

    private data class Segment(
        val freqHz: Double,
        val durationMs: Int,
    )

    companion object {
        private const val SAMPLE_RATE = 44100
    }
}
