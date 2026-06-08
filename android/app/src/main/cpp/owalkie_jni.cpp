#include <jni.h>

#include <android/log.h>
#include <android/multinetwork.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "owalkie_core.h"
#include "owalkie/client_events.hpp"
#include "owalkie/session_manager.hpp"

namespace {

constexpr const char* kLogTag = "OwalkieRelay";

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
std::atomic<long long> g_networkHandle{0};
std::atomic<bool> g_preConnectHookInstalled{false};

void ensurePreConnectHook() {
    bool expected = false;
    if (!g_preConnectHookInstalled.compare_exchange_strong(expected, true)) {
        return;
    }
    owalkie::SessionManager::instance().setPreConnectHook([]() {
        const long long stored = g_networkHandle.load(std::memory_order_relaxed);
        const net_handle_t handle =
            stored == 0LL ? NETWORK_UNSPECIFIED : static_cast<net_handle_t>(stored);
        const int rc = android_setprocnetwork(handle);
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "preConnect bindProcessNetwork handle=%lld rc=%d",
            static_cast<long long>(stored),
            rc);
    });
}

pthread_once_t g_workerJniOnce = PTHREAD_ONCE_INIT;
pthread_key_t g_workerJniKey;

void workerJniThreadDetach(void* value) {
    if (!value) {
        return;
    }
    static_cast<JavaVM*>(value)->DetachCurrentThread();
}

void workerJniKeyInit() {
    (void)pthread_key_create(&g_workerJniKey, workerJniThreadDetach);
}

// Long-lived owalkie worker threads (WS/UDP) must stay attached; do not detach per callback.
JNIEnv* getWorkerEnv(JavaVM* vm) {
    if (!vm) {
        return nullptr;
    }
    pthread_once(&g_workerJniOnce, workerJniKeyInit);
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        return env;
    }
    if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        return nullptr;
    }
    (void)pthread_setspecific(g_workerJniKey, vm);
    return env;
}

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

void clearPendingJniException(JNIEnv* jni) {
    if (jni && jni->ExceptionCheck()) {
        jni->ExceptionDescribe();
        jni->ExceptionClear();
    }
}

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
    if (!binding || !binding->jvm) {
        return;
    }
    // Teardown must not DeleteGlobalRef(listener) while dispatchEvent is in CallVoidMethod.
    for (int i = 0; i < 500 && binding->jniCallbackDepth.load(std::memory_order_acquire) > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    JniEnv env(binding->jvm);
    releaseBindingRefs(env.get(), binding.get());
}

void dispatchEvent(JniSessionBinding* binding, int eventType, const char* info) {
    if (!binding || !binding->jvm || !binding->listener || !binding->onEvent) {
        return;
    }
    JNIEnv* jni = getWorkerEnv(binding->jvm);
    if (!jni) {
        return;
    }
    binding->jniCallbackDepth.fetch_add(1, std::memory_order_acq_rel);
    jstring jInfo = info ? jni->NewStringUTF(info) : nullptr;
    jni->CallVoidMethod(
        binding->listener,
        binding->onEvent,
        static_cast<jlong>(binding->id),
        eventType,
        jInfo);
    clearPendingJniException(jni);
    if (jInfo) {
        jni->DeleteLocalRef(jInfo);
    }
    binding->jniCallbackDepth.fetch_sub(1, std::memory_order_acq_rel);
}

void dispatchRxPcm(
    JniSessionBinding* binding,
    std::span<const int16_t> pcm,
    int sampleRate,
    int packetMs) {
    if (!binding || !binding->jvm || !binding->listener || !binding->onRxPcm || pcm.empty()) {
        return;
    }
    JNIEnv* jni = getWorkerEnv(binding->jvm);
    if (!jni) {
        return;
    }
    binding->jniCallbackDepth.fetch_add(1, std::memory_order_acq_rel);
    jshortArray arr = jni->NewShortArray(static_cast<jsize>(pcm.size()));
    if (!arr) {
        binding->jniCallbackDepth.fetch_sub(1, std::memory_order_acq_rel);
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
    clearPendingJniException(jni);
    jni->DeleteLocalRef(arr);
    binding->jniCallbackDepth.fetch_sub(1, std::memory_order_acq_rel);
}

void dispatchSessionEvent(const std::shared_ptr<JniSessionBinding>& binding, const owalkie::Event& ev) {
    if (!binding || !owalkie::client_events::isVisible(ev.type)) {
        return;
    }
    std::string info;
    const char* infoPtr = nullptr;
    switch (ev.type) {
        case owalkie::EventType::Connected:
            break;
        case owalkie::EventType::ConnectionFailed:
        case owalkie::EventType::Disconnected:
        case owalkie::EventType::ConnectionLost:
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
    const int publicType = static_cast<int>(owalkie::client_events::toPublic(ev.type));
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "event session=%lld type=%d info=%s",
        static_cast<long long>(binding->id),
        publicType,
        infoPtr ? infoPtr : "");
    dispatchEvent(binding.get(), publicType, infoPtr);
}

owalkie::SessionCallbacks makeSessionCallbacks(const std::shared_ptr<JniSessionBinding>& binding) {
    owalkie::SessionCallbacks callbacks{};
    callbacks.onRxPcm = [binding](std::span<const int16_t> pcm, int sampleRate, int packetMs) {
        dispatchRxPcm(binding.get(), pcm, sampleRate, packetMs);
    };
    callbacks.onSessionEvent = [binding](const owalkie::Event& event) {
        dispatchSessionEvent(binding, event);
        if (event.type == owalkie::EventType::ConnectionFailed ||
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
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativePrepareConnection(
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

    ensurePreConnectHook();

    const owalkie::SessionId id = owalkie::SessionManager::instance().prepareConnection(
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

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeConnect(JNIEnv*, jobject, jlong sessionId, jint timeoutMs) {
    if (sessionId <= 0) {
        return static_cast<jint>(owalkie::Result::InvalidArg);
    }
    const owalkie::SessionId id = static_cast<owalkie::SessionId>(sessionId);
    ensurePreConnectHook();
    const owalkie::Result result =
        owalkie::SessionManager::instance().connect(id, timeoutMs < 0 ? 0 : timeoutMs);
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "nativeConnect session=%lld timeout=%d result=%d",
        static_cast<long long>(id),
        timeoutMs,
        static_cast<int>(result));
    return static_cast<jint>(result);
}

extern "C" JNIEXPORT void JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeDisconnect(JNIEnv*, jobject, jlong sessionId) {
    if (sessionId <= 0) {
        return;
    }
    const owalkie::SessionId id = static_cast<owalkie::SessionId>(sessionId);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "nativeDisconnect session=%lld", static_cast<long long>(id));
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
    if (env->GetArrayLength(outInts) < 9) {
        return static_cast<jint>(OWALKIE_ERR_INVALID_ARG);
    }
    owalkie_session_info info{};
    const owalkie_result r = owalkie_get_session_info(
        static_cast<owalkie_session_id>(sessionId), &info, nullptr, 0);
    if (r != OWALKIE_OK) {
        return static_cast<jint>(r);
    }
    jint values[9]{
        info.ready,
        info.connected,
        info.udp_ready,
        info.connection_lost,
        info.receiving,
        info.local_tx_active,
        info.ptt_server_locked,
        info.ptt_lock_display_sec,
        info.has_config,
    };
    env->SetIntArrayRegion(outInts, 0, 9, values);
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
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeTxSubmit(
    JNIEnv* env,
    jobject,
    jlong sessionId,
    jint op,
    jshortArray pcm) {
    if (sessionId <= 0) {
        return static_cast<jint>(OWALKIE_ERR_INVALID_ARG);
    }
    const auto txOp = static_cast<owalkie_tx_op>(op);
    if (txOp == OWALKIE_TX_PCM) {
        if (!pcm) {
            return static_cast<jint>(OWALKIE_ERR_INVALID_ARG);
        }
        const jsize len = env->GetArrayLength(pcm);
        if (len <= 0) {
            return static_cast<jint>(OWALKIE_ERR_INVALID_ARG);
        }
        std::vector<int16_t> buf(static_cast<size_t>(len));
        env->GetShortArrayRegion(pcm, 0, len, reinterpret_cast<jshort*>(buf.data()));
        return static_cast<jint>(owalkie_tx_submit(
            static_cast<owalkie_session_id>(sessionId),
            txOp,
            buf.data(),
            buf.size(),
            nullptr,
            0));
    }
    return static_cast<jint>(owalkie_tx_submit(
        static_cast<owalkie_session_id>(sessionId),
        txOp,
        nullptr,
        0,
        nullptr,
        0));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeTxWaitIdle(
    JNIEnv*,
    jobject,
    jlong sessionId,
    jint timeoutMs) {
    if (sessionId <= 0) {
        return JNI_FALSE;
    }
    return owalkie_tx_wait_idle(static_cast<owalkie_session_id>(sessionId), timeoutMs) != 0
        ? JNI_TRUE
        : JNI_FALSE;
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
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeBindProcessNetwork(JNIEnv*, jobject, jlong networkHandle) {
    g_networkHandle.store(static_cast<long long>(networkHandle), std::memory_order_relaxed);
    ensurePreConnectHook();
    const net_handle_t handle =
        networkHandle == 0L ? NETWORK_UNSPECIFIED : static_cast<net_handle_t>(networkHandle);
    const int rc = android_setprocnetwork(handle);
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "bindProcessNetwork handle=%lld rc=%d errno=%d",
        static_cast<long long>(networkHandle),
        rc,
        rc != 0 ? errno : 0);
    return rc == 0 ? 0 : -1;
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

extern "C" JNIEXPORT jint JNICALL
Java_ru_outsidepro_1arts_owalkie_OwalkieNative_nativeCheckChannelActivity(
    JNIEnv* env,
    jobject,
    jstring host,
    jint port,
    jstring channel,
    jint timeoutMs,
    jintArray outActive) {
    if (!host || !channel || !outActive) {
        return OWALKIE_ERR_INVALID_ARG;
    }
    if (env->GetArrayLength(outActive) < 1) {
        return OWALKIE_ERR_INVALID_ARG;
    }

    const char* hostUtf = env->GetStringUTFChars(host, nullptr);
    const char* channelUtf = env->GetStringUTFChars(channel, nullptr);
    owalkie_connect_params params{};
    params.host = hostUtf;
    params.port = port;
    params.channel = channelUtf;
    params.use_tls = 0;
    params.repeater_mode = 0;

    jint activeFlag = 0;
    const owalkie_result rc = owalkie_check_channel_activity(
        &params,
        timeoutMs < 0 ? 0 : timeoutMs,
        &activeFlag);

    env->ReleaseStringUTFChars(host, hostUtf);
    env->ReleaseStringUTFChars(channel, channelUtf);

    if (rc == OWALKIE_OK) {
        env->SetIntArrayRegion(outActive, 0, 1, &activeFlag);
    }
    return rc;
}
