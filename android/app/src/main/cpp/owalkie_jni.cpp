#include <jni.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "owalkie_core.h"
#include "owalkie/client_events.hpp"
#include "owalkie/session_manager.hpp"

namespace {

struct JniSessionBinding {
    owalkie::SessionId id = owalkie::kInvalidSessionId;
    JavaVM* jvm = nullptr;
    jobject listener = nullptr;
    jclass listenerClass = nullptr;
    jmethodID onEvent = nullptr;
    jmethodID onRxPcm = nullptr;
    std::atomic<int> jniCallbackDepth{0};
};

std::mutex g_jniMu;
std::unordered_map<owalkie::SessionId, std::shared_ptr<JniSessionBinding>> g_jniBindings;

class JniEnv {
public:
    explicit JniEnv(JavaVM* vm) : vm_(vm) {
        if (!vm_) {
            return;
        }
        if (vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) != JNI_OK) {
            if (vm_->AttachCurrentThread(&env_, nullptr) == JNI_OK) {
                attached_ = true;
            } else {
                env_ = nullptr;
            }
        }
    }

    ~JniEnv() {
        if (attached_ && vm_) {
            vm_->DetachCurrentThread();
        }
    }

    JniEnv(const JniEnv&) = delete;
    JniEnv& operator=(const JniEnv&) = delete;

    JNIEnv* get() const { return env_; }
    explicit operator bool() const { return env_ != nullptr; }

private:
    JavaVM* vm_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

void releaseBindingRefs(JNIEnv* env, JniSessionBinding* binding) {
    if (!binding || !env) {
        return;
    }
    if (binding->listener) {
        env->DeleteGlobalRef(binding->listener);
        binding->listener = nullptr;
    }
    if (binding->listenerClass) {
        env->DeleteGlobalRef(binding->listenerClass);
        binding->listenerClass = nullptr;
    }
}

void eraseBinding(owalkie::SessionId id) {
    std::shared_ptr<JniSessionBinding> binding;
    {
        std::lock_guard<std::mutex> lock(g_jniMu);
        const auto it = g_jniBindings.find(id);
        if (it == g_jniBindings.end()) {
            return;
        }
        binding = it->second;
        g_jniBindings.erase(it);
    }
    if (binding && binding->jvm) {
        JniEnv env(binding->jvm);
        releaseBindingRefs(env.get(), binding.get());
    }
}

void dispatchEvent(JniSessionBinding* binding, int eventType, const char* info) {
    if (!binding || !binding->jvm || !binding->listener || !binding->onEvent) {
        return;
    }
    JniEnv env(binding->jvm);
    if (!env) {
        return;
    }
    JNIEnv* jni = env.get();
    binding->jniCallbackDepth.fetch_add(1);
    jstring jInfo = info ? jni->NewStringUTF(info) : nullptr;
    jni->CallVoidMethod(
        binding->listener,
        binding->onEvent,
        static_cast<jlong>(binding->id),
        eventType,
        jInfo);
    if (jInfo) {
        jni->DeleteLocalRef(jInfo);
    }
    binding->jniCallbackDepth.fetch_sub(1);
}

void dispatchRxPcm(
    JniSessionBinding* binding,
    std::span<const int16_t> pcm,
    int sampleRate,
    int packetMs) {
    if (!binding || !binding->jvm || !binding->listener || !binding->onRxPcm || pcm.empty()) {
        return;
    }
    JniEnv env(binding->jvm);
    if (!env) {
        return;
    }
    JNIEnv* jni = env.get();
    binding->jniCallbackDepth.fetch_add(1);
    jshortArray arr = jni->NewShortArray(static_cast<jsize>(pcm.size()));
    if (!arr) {
        binding->jniCallbackDepth.fetch_sub(1);
        return;
    }
    jni->SetShortArrayRegion(
        arr,
        0,
        static_cast<jsize>(pcm.size()),
        reinterpret_cast<const jshort*>(pcm.data()));
    jni->CallVoidMethod(
        binding->listener,
        binding->onRxPcm,
        static_cast<jlong>(binding->id),
        arr,
        sampleRate,
        packetMs);
    jni->DeleteLocalRef(arr);
    binding->jniCallbackDepth.fetch_sub(1);
}

void dispatchSessionEvent(const std::shared_ptr<JniSessionBinding>& binding, const owalkie::Event& ev) {
    if (!binding || !owalkie::client_events::isVisible(ev.type)) {
        return;
    }
    std::string info;
    const char* infoPtr = nullptr;
    switch (ev.type) {
        case owalkie::EventType::SessionReady:
            break;
        case owalkie::EventType::ConnectFailed:
        case owalkie::EventType::Disconnected:
            infoPtr = ev.disconnectReason.c_str();
            break;
        case owalkie::EventType::ProtocolError:
            infoPtr = ev.protocolError.c_str();
            break;
        case owalkie::EventType::RxBroadcastStart:
            info = ev.rxBusyMode ? "true" : "false";
            infoPtr = info.c_str();
            break;
        case owalkie::EventType::PttLocked:
            info = std::to_string(ev.pttDisplaySec);
            infoPtr = info.c_str();
            break;
        default:
            break;
    }
    dispatchEvent(
        binding.get(),
        static_cast<int>(owalkie::client_events::toPublic(ev.type)),
        infoPtr);
}

owalkie::SessionCallbacks makeSessionCallbacks(const std::shared_ptr<JniSessionBinding>& binding) {
    owalkie::SessionCallbacks callbacks{};
    callbacks.onRxPcm = [binding](std::span<const int16_t> pcm, int sampleRate, int packetMs) {
        dispatchRxPcm(binding.get(), pcm, sampleRate, packetMs);
    };
    callbacks.onSessionEvent = [binding](const owalkie::Event& event) {
        dispatchSessionEvent(binding, event);
        if (event.type == owalkie::EventType::ConnectFailed ||
            event.type == owalkie::EventType::Disconnected ||
            event.type == owalkie::EventType::ProtocolError) {
            eraseBinding(binding->id);
        }
    };
    return callbacks;
}

owalkie::PowerProfile mapPower(jint profile) {
    switch (profile) {
        case 1:
            return owalkie::PowerProfile::Background;
        case 2:
            return owalkie::PowerProfile::ActiveTx;
        default:
            return owalkie::PowerProfile::Foreground;
    }
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeConnect(
    JNIEnv* env,
    jobject,
    jobject listener,
    jstring host,
    jint port,
    jstring channel,
    jboolean repeater) {
    if (!listener || !host || !channel) {
        return 0L;
    }

    auto binding = std::make_shared<JniSessionBinding>();
    if (env->GetJavaVM(&binding->jvm) != JNI_OK) {
        return 0L;
    }
    binding->listener = env->NewGlobalRef(listener);
    const jclass localClass = env->GetObjectClass(listener);
    binding->listenerClass = reinterpret_cast<jclass>(env->NewGlobalRef(localClass));
    binding->onEvent = env->GetMethodID(binding->listenerClass, "onNativeEvent", "(JILjava/lang/String;)V");
    binding->onRxPcm = env->GetMethodID(binding->listenerClass, "onNativeRxPcm", "(J[SII)V");
    if (!binding->onEvent || !binding->onRxPcm) {
        releaseBindingRefs(env, binding.get());
        return 0L;
    }

    const char* hostUtf = env->GetStringUTFChars(host, nullptr);
    const char* channelUtf = env->GetStringUTFChars(channel, nullptr);
    owalkie::ConnectParams params{};
    params.host = hostUtf;
    params.port = port;
    params.channel = channelUtf;
    params.repeaterMode = repeater == JNI_TRUE;

    const owalkie::SessionId id = owalkie::SessionManager::instance().connect(
        params,
        makeSessionCallbacks(binding),
        [binding](owalkie::SessionId allocated) {
            binding->id = allocated;
            std::lock_guard<std::mutex> lock(g_jniMu);
            g_jniBindings[allocated] = binding;
        });

    env->ReleaseStringUTFChars(host, hostUtf);
    env->ReleaseStringUTFChars(channel, channelUtf);

    if (id == owalkie::kInvalidSessionId) {
        releaseBindingRefs(env, binding.get());
        return 0L;
    }

    owalkie::SessionManager::instance().setPowerProfile(id, owalkie::PowerProfile::Foreground);
    return static_cast<jlong>(id);
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeDisconnect(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return;
    }
    const owalkie::SessionId id = static_cast<owalkie::SessionId>(sessionId);
    owalkie::SessionManager::instance().disconnect(id);
    eraseBinding(id);
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeHaltAndRelease(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return;
    }
    const owalkie::SessionId id = static_cast<owalkie::SessionId>(sessionId);
    owalkie::SessionManager::instance().haltAndRelease(id);
    eraseBinding(id);
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeDisconnectAll(JNIEnv*, jobject) {
    owalkie::SessionManager::instance().disconnectAll();
    std::vector<owalkie::SessionId> ids;
    {
        std::lock_guard<std::mutex> lock(g_jniMu);
        ids.reserve(g_jniBindings.size());
        for (const auto& entry : g_jniBindings) {
            ids.push_back(entry.first);
        }
    }
    for (const owalkie::SessionId id : ids) {
        eraseBinding(id);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeDisconnectAllAndWait(JNIEnv*, jobject, jint timeoutMs) {
    owalkie::SessionManager::instance().disconnectAllAndWait(timeoutMs < 0 ? 3000 : timeoutMs);
    std::vector<owalkie::SessionId> ids;
    {
        std::lock_guard<std::mutex> lock(g_jniMu);
        ids.reserve(g_jniBindings.size());
        for (const auto& entry : g_jniBindings) {
            ids.push_back(entry.first);
        }
    }
    for (const owalkie::SessionId id : ids) {
        eraseBinding(id);
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeSessionValid(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return 0;
    }
    return owalkie::SessionManager::instance().isValid(static_cast<owalkie::SessionId>(sessionId)) ? 1 : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeSessionReady(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return 0;
    }
    return owalkie::SessionManager::instance().isSessionReady(static_cast<owalkie::SessionId>(sessionId)) ? 1 : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeGetSessionInfoFlags(
    JNIEnv* env,
    jobject,
    jlong sessionId,
    jintArray outInts) {
    if (sessionId <= 0 || !outInts) {
        return static_cast<jint>(OWALKIE_ERR_INVALID_ARG);
    }
    if (env->GetArrayLength(outInts) < 8) {
        return static_cast<jint>(OWALKIE_ERR_INVALID_ARG);
    }
    owalkie_session_info info{};
    const owalkie_result r = owalkie_get_session_info(
        static_cast<owalkie_session_id>(sessionId), &info, nullptr, 0);
    if (r != OWALKIE_OK) {
        return static_cast<jint>(r);
    }
    jint values[8]{
        info.ready,
        info.connected,
        info.udp_ready,
        info.receiving,
        info.local_tx_active,
        info.ptt_server_locked,
        info.ptt_lock_display_sec,
        info.has_config,
    };
    env->SetIntArrayRegion(outInts, 0, 8, values);
    return static_cast<jint>(OWALKIE_OK);
}

extern "C" JNIEXPORT jstring JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeGetSessionInfoConfig(
    JNIEnv* env,
    jobject,
    jlong sessionId,
    jintArray outInts) {
    if (sessionId <= 0 || !outInts) {
        return nullptr;
    }
    if (env->GetArrayLength(outInts) < 10) {
        return nullptr;
    }
    owalkie_session_info info{};
    char opusApp[32]{};
    if (owalkie_get_session_info(
            static_cast<owalkie_session_id>(sessionId), &info, opusApp, sizeof(opusApp)) !=
            OWALKIE_OK ||
        !info.has_config) {
        return nullptr;
    }
    const owalkie_welcome_config& c = info.config;
    jint values[10]{
        static_cast<jint>(c.session_id),
        c.protocol_version,
        c.sample_rate,
        c.packet_ms,
        c.busy_mode,
        c.transmit_timeout_sec,
        c.opus_bitrate,
        c.opus_complexity,
        c.opus_fec,
        c.opus_dtx,
    };
    env->SetIntArrayRegion(outInts, 0, 10, values);
    return env->NewStringUTF(opusApp);
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeTxStart(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    return static_cast<jint>(owalkie_tx_start(static_cast<owalkie_session_id>(sessionId)));
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativePushTxPcm(
    JNIEnv* env,
    jobject,
    jlong sessionId,
    jshortArray pcm) {
    if (sessionId <= 0 || !pcm) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    const jsize len = env->GetArrayLength(pcm);
    if (len <= 0) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    std::vector<int16_t> buf(static_cast<size_t>(len));
    env->GetShortArrayRegion(pcm, 0, len, reinterpret_cast<jshort*>(buf.data()));
    return static_cast<jint>(owalkie_push_tx_pcm(
        static_cast<owalkie_session_id>(sessionId),
        buf.data(),
        buf.size()));
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeTxEnd(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    return static_cast<jint>(owalkie_tx_end(static_cast<owalkie_session_id>(sessionId)));
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeSetRepeater(
    JNIEnv*,
    jobject,
    jlong sessionId,
    jboolean enabled) {
    if (sessionId <= 0) {
        return;
    }
    (void)owalkie::SessionManager::instance().setRepeaterMode(
        static_cast<owalkie::SessionId>(sessionId),
        enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeSetPowerProfile(
    JNIEnv*,
    jobject,
    jlong sessionId,
    jint profile) {
    if (sessionId <= 0) {
        return;
    }
    owalkie::SessionManager::instance().setPowerProfile(
        static_cast<owalkie::SessionId>(sessionId),
        mapPower(profile));
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativePunchNat(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    return static_cast<jint>(owalkie::SessionManager::instance().punchNat(
        static_cast<owalkie::SessionId>(sessionId)));
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeReportSignal(JNIEnv*, jobject, jint mode, jint value) {
    const owalkie_signal_mode cMode = mode == 1 ? OWALKIE_SIGNAL_CELL : OWALKIE_SIGNAL_WIFI;
    return static_cast<jint>(owalkie_report_signal(cMode, value));
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeClearSignal(JNIEnv*, jobject, jint mode) {
    const owalkie_signal_mode cMode = mode == 1 ? OWALKIE_SIGNAL_CELL : OWALKIE_SIGNAL_WIFI;
    return static_cast<jint>(owalkie_clear_signal(cMode));
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeGetUplinkSignalByte(JNIEnv*, jobject) {
    return owalkie_get_uplink_signal_byte();
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeSetActivityFocused(
    JNIEnv*,
    jobject,
    jlong sessionId,
    jboolean focused) {
    if (sessionId <= 0) {
        return;
    }
    owalkie::SessionManager::instance().setPowerProfile(
        static_cast<owalkie::SessionId>(sessionId),
        focused ? owalkie::PowerProfile::Foreground : owalkie::PowerProfile::Background);
}
