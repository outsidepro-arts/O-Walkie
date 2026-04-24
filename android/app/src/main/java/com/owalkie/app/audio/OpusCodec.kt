package com.owalkie.app.audio

import java.nio.ByteBuffer
import java.nio.ByteOrder

interface OpusCodec {
    fun encode(pcm: ShortArray): ByteArray
    fun decode(opus: ByteArray, frameSize: Int): ShortArray
}

class OpusCodecFactory {
    fun create(sampleRate: Int, channels: Int): OpusCodec {
        return try {
            ReflectiveKopusCodec(sampleRate, channels)
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

private class ReflectiveKopusCodec(
    private val sampleRate: Int,
    private val channels: Int,
) : OpusCodec {
    private val encoder: Any
    private val decoder: Any
    private val encoderClass: Class<*>
    private val decoderClass: Class<*>

    init {
        val appClass = Class.forName("eu.buney.kopus.OpusApplication")
        val appVoip = java.lang.Enum.valueOf(appClass as Class<out Enum<*>>, "Voip")

        encoderClass = Class.forName("eu.buney.kopus.OpusEncoder")
        decoderClass = Class.forName("eu.buney.kopus.OpusDecoder")

        encoder = encoderClass
            .getConstructor(Int::class.javaPrimitiveType, Int::class.javaPrimitiveType, appClass)
            .newInstance(sampleRate, channels, appVoip)
        decoder = decoderClass
            .getConstructor(Int::class.javaPrimitiveType, Int::class.javaPrimitiveType)
            .newInstance(sampleRate, channels)
    }

    override fun encode(pcm: ShortArray): ByteArray {
        val method = encoderClass.methods.firstOrNull {
            it.name == "encode" &&
                it.parameterTypes.size == 1 &&
                it.parameterTypes[0] == ShortArray::class.java
        } ?: throw IllegalStateException("Kopus encode(short[]) method not found")
        return method.invoke(encoder, pcm) as ByteArray
    }

    override fun decode(opus: ByteArray, frameSize: Int): ShortArray {
        val method = decoderClass.methods.firstOrNull {
            it.name == "decode" &&
                it.parameterTypes.size == 2 &&
                it.parameterTypes[0] == ByteArray::class.java &&
                it.parameterTypes[1] == Int::class.javaPrimitiveType
        } ?: throw IllegalStateException("Kopus decode(byte[],int) method not found")
        return method.invoke(decoder, opus, frameSize) as ShortArray
    }
}

