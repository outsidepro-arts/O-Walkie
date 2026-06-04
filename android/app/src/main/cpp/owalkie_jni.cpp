#include <jni.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "owalkie/session_manager.hpp"

namespace {

struct JniSessionBinding {
    owalkie::SessionId id = owalkie::kInvalidSessionId;
    JavaVM* jvm = nullptr;
    jobject listener = nullptr;
    jclass listenerClass = nullptr;
    jmethodID onEvent = nullptr;
    jmethodID onRxOpus = nullptr;
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

void dispatchRxOpus(JniSessionBinding* binding, std::span<const uint8_t> opus) {
    if (!binding || !binding->jvm || !binding->listener || !binding->onRxOpus || opus.empty()) {
        return;
    }
    JniEnv env(binding->jvm);
    if (!env) {
        return;
    }
    JNIEnv* jni = env.get();
    binding->jniCallbackDepth.fetch_add(1);
    jbyteArray arr = jni->NewByteArray(static_cast<jsize>(opus.size()));
    if (!arr) {
        binding->jniCallbackDepth.fetch_sub(1);
        return;
    }
    jni->SetByteArrayRegion(
        arr,
        0,
        static_cast<jsize>(opus.size()),
        reinterpret_cast<const jbyte*>(opus.data()));
    jni->CallVoidMethod(binding->listener, binding->onRxOpus, static_cast<jlong>(binding->id), arr);
    jni->DeleteLocalRef(arr);
    binding->jniCallbackDepth.fetch_sub(1);
}

std::string welcomeJson(const owalkie::WelcomeConfig& w) {
    std::ostringstream os;
    os << "{\"type\":\"welcome\""
       << ",\"protocolVersion\":" << w.protocolVersion
       << ",\"sessionId\":" << w.sessionId
       << ",\"sampleRate\":" << w.sampleRate
       << ",\"packetMs\":" << w.packetMs
       << ",\"busyMode\":" << (w.busyMode ? "true" : "false")
       << ",\"transmitTimeoutSec\":" << w.transmitTimeoutSec
       << ",\"opus\":{"
       << "\"bitrate\":" << w.opus.bitrate
       << ",\"complexity\":" << w.opus.complexity
       << ",\"fec\":" << (w.opus.fec ? "true" : "false")
       << ",\"dtx\":" << (w.opus.dtx ? "true" : "false")
       << ",\"application\":\"" << w.opus.application << "\""
       << "}}";
    return os.str();
}

void dispatchSessionEvent(const std::shared_ptr<JniSessionBinding>& binding, const owalkie::Event& ev) {
    if (!binding) {
        return;
    }
    std::string info;
    const char* infoPtr = nullptr;
    switch (ev.type) {
        case owalkie::EventType::Welcome:
        case owalkie::EventType::SessionReady:
            info = welcomeJson(ev.welcome);
            infoPtr = info.c_str();
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
    dispatchEvent(binding.get(), static_cast<int>(ev.type), infoPtr);
}

owalkie::SessionCallbacks makeSessionCallbacks(const std::shared_ptr<JniSessionBinding>& binding) {
    owalkie::SessionCallbacks callbacks{};
    callbacks.onRxOpus = [binding](std::span<const uint8_t> opus) {
        dispatchRxOpus(binding.get(), opus);
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
    binding->onRxOpus = env->GetMethodID(binding->listenerClass, "onNativeRxOpus", "(J[B)V");
    if (!binding->onEvent || !binding->onRxOpus) {
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
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeSendTxOpus(
    JNIEnv* env,
    jobject,
    jlong sessionId,
    jbyteArray opus,
    jint signal) {
    if (sessionId <= 0 || !opus) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    const jsize len = env->GetArrayLength(opus);
    if (len <= 0) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    std::vector<uint8_t> buf(static_cast<size_t>(len));
    env->GetByteArrayRegion(opus, 0, len, reinterpret_cast<jbyte*>(buf.data()));
    return static_cast<jint>(owalkie::SessionManager::instance().sendTxOpus(
        static_cast<owalkie::SessionId>(sessionId),
        buf,
        signal));
}

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeSendTxEof(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    return static_cast<jint>(owalkie::SessionManager::instance().sendTxEofBurst(
        static_cast<owalkie::SessionId>(sessionId)));
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

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeSetTxSignal(JNIEnv*, jobject, jlong sessionId, jint signal) {
    if (sessionId <= 0) {
        return;
    }
    owalkie::SessionManager::instance().setTxSignalStrength(static_cast<owalkie::SessionId>(sessionId), signal);
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeNotifyNetworkChanged(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return;
    }
    owalkie::SessionManager::instance().notifyNetworkChanged(static_cast<owalkie::SessionId>(sessionId));
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
