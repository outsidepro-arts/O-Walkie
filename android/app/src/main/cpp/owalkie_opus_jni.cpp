#include <jni.h>

#include <memory>
#include <vector>

#include "opus_codec.hpp"

namespace {

struct OpusJniHandle {
    owalkie::codec::OpusCodec codec;
    int frameSamples = 0;
};

OpusJniHandle* fromOpusHandle(jlong handle) {
    return reinterpret_cast<OpusJniHandle*>(handle);
}

owalkie::WelcomeConfig makeWelcomeConfig(
    jint sampleRate,
    jint packetMs,
    jint bitrate,
    jint complexity,
    jboolean fec,
    jboolean dtx,
    jstring application,
    JNIEnv* env) {
    owalkie::WelcomeConfig cfg{};
    cfg.sampleRate = sampleRate;
    cfg.packetMs = packetMs > 0 ? packetMs : 20;
    cfg.opus.bitrate = bitrate;
    cfg.opus.complexity = complexity;
    cfg.opus.fec = fec == JNI_TRUE;
    cfg.opus.dtx = dtx == JNI_TRUE;
    if (application) {
        const char* utf = env->GetStringUTFChars(application, nullptr);
        if (utf) {
            cfg.opus.application = utf;
            env->ReleaseStringUTFChars(application, utf);
        }
    }
    return cfg;
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeCreateOpusCodec(
    JNIEnv* env,
    jobject,
    jint sampleRate,
    jint packetMs,
    jint bitrate,
    jint complexity,
    jboolean fec,
    jboolean dtx,
    jstring application) {
    auto* handle = new (std::nothrow) OpusJniHandle();
    if (!handle) {
        return 0;
    }
    const auto cfg = makeWelcomeConfig(
        sampleRate, packetMs, bitrate, complexity, fec, dtx, application, env);
    if (handle->codec.configure(cfg) != owalkie::Result::Ok) {
        delete handle;
        return 0;
    }
    handle->frameSamples = handle->codec.frameSamples();
    return reinterpret_cast<jlong>(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeDestroyOpusCodec(JNIEnv*, jobject, jlong handle) {
    delete fromOpusHandle(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeResetOpusCodec(JNIEnv*, jobject, jlong handle) {
    auto* h = fromOpusHandle(handle);
    if (h) {
        h->codec.reset();
        h->frameSamples = 0;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeOpusFrameSamples(JNIEnv*, jobject, jlong handle) {
    auto* h = fromOpusHandle(handle);
    return h ? h->frameSamples : 0;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeOpusEncode(
    JNIEnv* env,
    jobject,
    jlong handle,
    jshortArray pcm) {
    auto* h = fromOpusHandle(handle);
    if (!h || !pcm) {
        return nullptr;
    }
    const jsize len = env->GetArrayLength(pcm);
    if (len != h->frameSamples) {
        return nullptr;
    }
    std::vector<int16_t> samples(static_cast<size_t>(len));
    env->GetShortArrayRegion(pcm, 0, len, reinterpret_cast<jshort*>(samples.data()));

    std::vector<uint8_t> encoded;
    if (h->codec.encode(samples, encoded) != owalkie::Result::Ok || encoded.empty()) {
        return nullptr;
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(encoded.size()));
    if (!out) {
        return nullptr;
    }
    env->SetByteArrayRegion(
        out,
        0,
        static_cast<jsize>(encoded.size()),
        reinterpret_cast<const jbyte*>(encoded.data()));
    return out;
}

extern "C" JNIEXPORT jshortArray JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeOpusDecode(
    JNIEnv* env,
    jobject,
    jlong handle,
    jbyteArray opus,
    jint frameSamples) {
    auto* h = fromOpusHandle(handle);
    if (!h || !opus || frameSamples <= 0) {
        return nullptr;
    }
    const jsize len = env->GetArrayLength(opus);
    if (len <= 0) {
        return nullptr;
    }
    std::vector<uint8_t> packet(static_cast<size_t>(len));
    env->GetByteArrayRegion(opus, 0, len, reinterpret_cast<jbyte*>(packet.data()));

    std::vector<int16_t> pcm;
    if (h->codec.decode(packet, frameSamples, pcm) != owalkie::Result::Ok || pcm.empty()) {
        return nullptr;
    }
    jshortArray out = env->NewShortArray(static_cast<jsize>(pcm.size()));
    if (!out) {
        return nullptr;
    }
    env->SetShortArrayRegion(
        out,
        0,
        static_cast<jsize>(pcm.size()),
        reinterpret_cast<const jshort*>(pcm.data()));
    return out;
}
