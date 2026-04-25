package ru.outsidepro_arts.owalkie.audio

import eu.buney.kopus.OpusApplication
import eu.buney.kopus.OpusDecoder
import eu.buney.kopus.OpusEncoder
import java.nio.ByteBuffer
import java.nio.ByteOrder

interface OpusCodec {
    fun encode(pcm: ShortArray): ByteArray
    fun decode(opus: ByteArray, frameSize: Int): ShortArray
}

class OpusCodecFactory {
    fun create(sampleRate: Int, channels: Int): OpusCodec {
        return try {
            val codec = KopusCodec(sampleRate, channels)
            // Validate codec early to avoid runtime crash on first PTT.
            val probe = ShortArray((sampleRate / 50).coerceAtLeast(1))
            val encoded = codec.encode(probe)
            codec.decode(encoded, probe.size)
            codec
        } catch (_: Throwable) {
            // Keep app usable on dev devices where JNI Opus binding is unavailable.
            PcmPassthroughCodec()
        }
    }
}

private class PcmPassthroughCodec : OpusCodec {
    override fun encode(pcm: ShortArray): ByteArray {
        val bb = ByteBuffer.allocate(pcm.size * 2).order(ByteOrder.LITTLE_ENDIAN)
        pcm.forEach { bb.putShort(it) }
        return bb.array()
    }

    override fun decode(opus: ByteArray, frameSize: Int): ShortArray {
        val out = ShortArray(frameSize)
        val bb = ByteBuffer.wrap(opus).order(ByteOrder.LITTLE_ENDIAN)
        var i = 0
        while (bb.remaining() >= 2 && i < out.size) {
            out[i++] = bb.short
        }
        return out
    }
}

private class KopusCodec(
    private val sampleRate: Int,
    private val channels: Int,
) : OpusCodec {
    private val encoder = OpusEncoder(sampleRate, channels, OpusApplication.Voip)
    private val decoder = OpusDecoder(sampleRate, channels)

    override fun encode(pcm: ShortArray): ByteArray {
        val maxOpusPacket = 1275
        val out = ByteArray(maxOpusPacket)
        val encodedBytes = encoder.encode(
            pcm,
            0,
            pcm.size,
            out,
            0,
            out.size,
        )
        if (encodedBytes <= 0) {
            return ByteArray(0)
        }
        return out.copyOf(encodedBytes)
    }

    override fun decode(opus: ByteArray, frameSize: Int): ShortArray {
        val out = ShortArray(frameSize * channels)
        val decodedSamples = decoder.decode(
            opus,
            0,
            opus.size,
            out,
            0,
            frameSize,
            false,
        )
        if (decodedSamples <= 0) {
            return ShortArray(frameSize * channels)
        }
        val actual = (decodedSamples * channels).coerceAtMost(out.size)
        return out.copyOf(actual)
    }
}

