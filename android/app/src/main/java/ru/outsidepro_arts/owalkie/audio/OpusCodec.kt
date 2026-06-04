package ru.outsidepro_arts.owalkie.audio

import ru.outsidepro_arts.owalkie.BuildConfig
import ru.outsidepro_arts.owalkie.OwalkieNative
import java.nio.ByteBuffer
import java.nio.ByteOrder

interface OpusCodec {
    fun encode(pcm: ShortArray): ByteArray
    fun decode(opus: ByteArray, frameSize: Int): ShortArray
}

data class OpusConfig(
    val bitrate: Int = 12000,
    val complexity: Int = 5,
    val fec: Boolean = true,
    val dtx: Boolean = false,
    val application: String = "voip",
    val packetMs: Int = 20,
)

private fun OpusConfig.packetMsForCodec(): Int = packetMs.coerceIn(10, 60)

class OpusCodecFactory {
    fun create(sampleRate: Int, channels: Int, config: OpusConfig = OpusConfig()): OpusCodec {
        if (channels != 1) {
            return PcmPassthroughCodec()
        }
        if (!BuildConfig.BUILD_NATIVE_RELAY) {
            return PcmPassthroughCodec()
        }
        return try {
            val codec = NativeOpusCodec(sampleRate, config, config.packetMsForCodec())
            val probe = ShortArray((sampleRate / 50).coerceAtLeast(1))
            val encoded = codec.encode(probe)
            codec.decode(encoded, probe.size)
            codec
        } catch (_: Throwable) {
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

private class NativeOpusCodec(
    sampleRate: Int,
    private val config: OpusConfig,
    private val packetMs: Int,
) : OpusCodec {
    private val handle: Long

    init {
        OwalkieNative.ensureLoaded()
        handle = OwalkieNative.nativeCreateOpusCodec(
            sampleRate,
            packetMs,
            config.bitrate,
            config.complexity,
            config.fec,
            config.dtx,
            config.application,
        )
        if (handle == 0L) {
            throw IllegalStateException("native opus codec init failed")
        }
    }

    override fun encode(pcm: ShortArray): ByteArray {
        return OwalkieNative.nativeOpusEncode(handle, pcm) ?: ByteArray(0)
    }

    override fun decode(opus: ByteArray, frameSize: Int): ShortArray {
        return OwalkieNative.nativeOpusDecode(handle, opus, frameSize)
            ?: ShortArray(frameSize)
    }

    fun close() {
        if (handle != 0L) {
            OwalkieNative.nativeDestroyOpusCodec(handle)
        }
    }
}
