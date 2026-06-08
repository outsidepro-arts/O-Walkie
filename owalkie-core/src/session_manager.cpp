#include "owalkie/session_manager.hpp"

#include "owalkie/protocol.hpp"
#include "owalkie/udp.hpp"

#include <algorithm>
#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace owalkie {
namespace {

constexpr int kTeardownWaitMs = 15000;
constexpr int kDefaultReconnectTimeoutMs = 3500;
constexpr int kConnectTransportWaitMs = 800;
void clientReconnectLog(const char* fmt, ...) {
#if defined(__ANDROID__)
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, "OwalkieRelay", fmt, args);
    va_end(args);
#else
    (void)fmt;
#endif
}

} // namespace

SessionManager& SessionManager::instance() {
    static SessionManager manager;
    return manager;
}

void SessionManager::signalUserDisconnect(const std::shared_ptr<ManagedSession>& managed) {
    if (!managed) {
        return;
    }
    managed->userIntent.store(false);
    if (managed->session) {
        managed->session->cancelOngoingConnect();
    }
}

bool SessionManager::takeSessionForTeardown(
    SessionId id,
    int timeoutMs,
    std::shared_ptr<ManagedSession>& out,
    std::unique_lock<std::mutex>& opLockOut) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        std::shared_ptr<ManagedSession> managed;
        {
            std::lock_guard<std::mutex> lock(mu_);
            const auto it = sessions_.find(id);
            if (it == sessions_.end()) {
                return false;
            }
            managed = it->second;
        }
        signalUserDisconnect(managed);
        std::unique_lock<std::mutex> opLock(managed->sessionOpMu, std::defer_lock);
        if (!opLock.try_lock()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            const auto it = sessions_.find(id);
            if (it == sessions_.end()) {
                return false;
            }
            out = it->second;
            sessions_.erase(it);
        }
        opLockOut = std::move(opLock);
        return true;
    }

    // connect() still holds sessionOpMu — leave session registered; caller retries teardown.
    return false;
}

Session* SessionManager::sessionLocked(SessionId id) const {
    const auto it = sessions_.find(id);
    if (it == sessions_.end() || !it->second || !it->second->session) {
        return nullptr;
    }
    return it->second->session.get();
}

SessionCallbacks SessionManager::wrapCallbacks(SessionId id, SessionCallbacks user) {
    SessionCallbacks wrapped = std::move(user);
    const auto userEvent = wrapped.onSessionEvent;
    wrapped.onSessionEvent = [userEvent](const Event& event) {
        if (userEvent) {
            userEvent(event);
        }
    };
    return wrapped;
}

void SessionManager::pruneJoinedCleanups() {
    std::lock_guard<std::mutex> lock(cleanupMu_);
    for (auto it = cleanupThreads_.begin(); it != cleanupThreads_.end();) {
        if (!it->joinable()) {
            it = cleanupThreads_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionManager::scheduleSessionCleanup(std::shared_ptr<ManagedSession> owned) {
    if (!owned || !owned->session) {
        return;
    }
    pendingCleanups_.fetch_add(1, std::memory_order_relaxed);
    std::thread([owned = std::move(owned), this]() mutable {
        struct PendingCleanupGuard {
            SessionManager* mgr;
            ~PendingCleanupGuard() {
                mgr->pendingCleanups_.fetch_sub(1, std::memory_order_relaxed);
            }
        } guard{this};
        if (owned && owned->session) {
            owned->session->waitUntilTransportStopped(-1);
        }
    }).detach();
}

void SessionManager::waitForCleanups(int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (pendingCleanups_.load(std::memory_order_relaxed) > 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    while (true) {
        pruneJoinedCleanups();
        std::vector<std::thread> pending;
        {
            std::lock_guard<std::mutex> lock(cleanupMu_);
            pending = std::move(cleanupThreads_);
            cleanupThreads_.clear();
        }
        if (pending.empty()) {
            return;
        }
        for (auto& worker : pending) {
            if (!worker.joinable()) {
                continue;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) {
                worker.detach();
                continue;
            }
            while (worker.joinable() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (worker.joinable()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    worker.detach();
                } else {
                    worker.join();
                }
            }
        }
        pruneJoinedCleanups();
        {
            std::lock_guard<std::mutex> lock(cleanupMu_);
            if (cleanupThreads_.empty()) {
                return;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return;
        }
    }
}

void SessionManager::releaseSession(SessionId id, bool callDisconnect) {
    (void)callDisconnect;
    std::shared_ptr<ManagedSession> owned;
    std::unique_lock<std::mutex> opLock;
    if (!takeSessionForTeardown(id, kTeardownWaitMs, owned, opLock)) {
        return;
    }
    if (!owned || !owned->session) {
        return;
    }
    owned->session->setCallbacks({});
    owned->session->disconnect();
    if (opLock.owns_lock()) {
        opLock.unlock();
    }
    scheduleSessionCleanup(std::move(owned));
}

Result SessionManager::connect(SessionId id, int timeoutMs) {
    std::shared_ptr<ManagedSession> managed;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = sessions_.find(id);
        if (it == sessions_.end() || !it->second || !it->second->session) {
            return Result::InvalidArg;
        }
        managed = it->second;
    }

    if (!managed->userIntent.load()) {
        return Result::InvalidArg;
    }

    Session* session = managed->session.get();
    if (session->isSessionReady()) {
        return Result::Ok;
    }

    const int connectTimeout =
        timeoutMs > 0 ? timeoutMs : kDefaultReconnectTimeoutMs;

    std::unique_lock<std::mutex> opLock(managed->sessionOpMu);
    if (!managed->userIntent.load()) {
        return Result::InvalidArg;
    }
    if (session->isSessionReady()) {
        return Result::Ok;
    }

    session->cancelOngoingConnect();
    session->haltTransport();

    session->waitUntilTransportStopped(kConnectTransportWaitMs);

    if (!managed->userIntent.load()) {
        return Result::InvalidArg;
    }
    if (session->isSessionReady()) {
        return Result::Ok;
    }

    if (session->isConnected() && !session->isSessionReady()) {
        const int welcomeWaitMs = std::min(connectTimeout, 2500);
        const auto welcomeDeadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(welcomeWaitMs);
        while (std::chrono::steady_clock::now() < welcomeDeadline && managed->userIntent.load()) {
            if (session->isSessionReady()) {
                return Result::Ok;
            }
            if (!session->isConnected()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (session->isSessionReady()) {
            return Result::Ok;
        }
    }

    session->reconnectTeardown();
    session->clearConnectionLostSignaled();

    if (!managed->userIntent.load()) {
        return Result::InvalidArg;
    }

    if (preConnectHook_) {
        preConnectHook_();
    }

    if (!managed->userIntent.load()) {
        return Result::InvalidArg;
    }

    const Result result = session->reconnectConnect(managed->params, connectTimeout);

    if (result == Result::Ok && !session->isSessionReady() && managed->userIntent.load()) {
        const int welcomeWaitMs = std::min(connectTimeout, 2500);
        const auto welcomeDeadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(welcomeWaitMs);
        while (std::chrono::steady_clock::now() < welcomeDeadline && managed->userIntent.load()) {
            if (session->isSessionReady()) {
                break;
            }
            if (!session->isConnected()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    clientReconnectLog(
        "connect session=%llu result=%d ready=%d timeout=%d",
        static_cast<unsigned long long>(id),
        static_cast<int>(result),
        session->isSessionReady() ? 1 : 0,
        connectTimeout);

    if (result == Result::Ok && session->isSessionReady()) {
        return Result::Ok;
    }

    if (result == Result::Ok) {
        return Result::Network;
    }

    if (result == Result::InvalidArg || result == Result::Unsupported || result == Result::Protocol) {
        session->emitConnectionFailed(
            result == Result::InvalidArg ? "invalid connect parameters" : "connect failed");
        releaseSession(id, false);
        return result;
    }

    return result;
}

SessionId SessionManager::prepareConnection(
    ConnectParams params,
    SessionCallbacks callbacks,
    std::function<void(SessionId id)> onAllocated) {
    std::unique_ptr<Session> session;
    if (Session::create(session) != Result::Ok) {
        return kInvalidSessionId;
    }

    SessionId id = kInvalidSessionId;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (sessions_.size() >= kMaxManagedSessions) {
            return kInvalidSessionId;
        }
        id = nextId_++;
        auto managed = std::make_shared<ManagedSession>();
        managed->id = id;
        managed->params = params;
        managed->userIntent.store(true);
        managed->session = std::move(session);
        managed->session->setCallbacks(wrapCallbacks(id, std::move(callbacks)));
        sessions_.emplace(id, managed);
    }

    if (onAllocated) {
        onAllocated(id);
    }

    return id;
}

void SessionManager::disconnect(SessionId id) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::shared_ptr<ManagedSession> owned;
        std::unique_lock<std::mutex> opLock;
        if (takeSessionForTeardown(id, kTeardownWaitMs, owned, opLock)) {
            if (!owned || !owned->session) {
                return;
            }
            owned->session->disconnect();
            if (opLock.owns_lock()) {
                opLock.unlock();
            }
            scheduleSessionCleanup(std::move(owned));
            return;
        }
        Session* session = nullptr;
        {
            std::lock_guard<std::mutex> lock(mu_);
            session = sessionLocked(id);
        }
        if (!session) {
            return;
        }
        session->cancelOngoingConnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void SessionManager::haltAndRelease(SessionId id) {
    std::shared_ptr<ManagedSession> owned;
    std::unique_lock<std::mutex> opLock;
    if (!takeSessionForTeardown(id, kTeardownWaitMs, owned, opLock)) {
        return;
    }
    if (!owned || !owned->session) {
        return;
    }
    owned->session->setCallbacks({});
    owned->session->haltTransport();
    if (opLock.owns_lock()) {
        opLock.unlock();
    }
    scheduleSessionCleanup(std::move(owned));
}

void SessionManager::disconnectAll() {
    std::vector<std::shared_ptr<ManagedSession>> managedSessions;
    {
        std::lock_guard<std::mutex> lock(mu_);
        managedSessions.reserve(sessions_.size());
        for (const auto& entry : sessions_) {
            managedSessions.push_back(entry.second);
        }
    }
    for (const auto& managed : managedSessions) {
        signalUserDisconnect(managed);
        if (managed && managed->session) {
            managed->session->cancelOngoingConnect();
        }
    }

    std::vector<SessionId> ids;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ids.reserve(sessions_.size());
        for (const auto& entry : sessions_) {
            ids.push_back(entry.first);
        }
    }
    for (const SessionId id : ids) {
        disconnect(id);
    }
}

void SessionManager::disconnectAllAndWait(int timeoutMs) {
    disconnectAll();
    waitForCleanups(timeoutMs);
}

void SessionManager::waitForPendingCleanups(int timeoutMs) {
    waitForCleanups(timeoutMs);
}

bool SessionManager::isValid(SessionId id) const {
    if (id == kInvalidSessionId) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mu_);
    return sessions_.find(id) != sessions_.end();
}

bool SessionManager::isSessionReady(SessionId id) const {
    std::lock_guard<std::mutex> lock(mu_);
    const Session* session = sessionLocked(id);
    return session != nullptr && session->isSessionReady();
}

Result SessionManager::getSessionInfo(SessionId id, SessionState* out_state, bool* out_ready) const {
    if (!out_state || !out_ready) {
        return Result::InvalidArg;
    }
    std::lock_guard<std::mutex> lock(mu_);
    const Session* session = sessionLocked(id);
    if (!session) {
        return Result::InvalidArg;
    }
    *out_state = session->state();
    *out_ready = session->isSessionReady();
    return Result::Ok;
}

Result SessionManager::submitTx(
    SessionId id,
    TxSubmitOp op,
    std::span<const int16_t> pcm,
    std::span<const uint8_t> opus) {
    Session* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        session = sessionLocked(id);
        if (!session) {
            return Result::InvalidArg;
        }
    }
    if (op != TxSubmitOp::Abort && !session->isSessionReady()) {
        return Result::NotReady;
    }
    return session->submitTx(op, pcm, opus);
}

bool SessionManager::waitTxQueueIdle(SessionId id, int timeoutMs) {
    std::lock_guard<std::mutex> lock(mu_);
    Session* session = sessionLocked(id);
    if (!session) {
        return true;
    }
    return session->waitTxQueueIdle(timeoutMs);
}

Result SessionManager::setRepeaterMode(SessionId id, bool enabled) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end() || !it->second || !it->second->session) {
        return Result::InvalidArg;
    }
    it->second->params.repeaterMode = enabled;
    return it->second->session->setRepeaterMode(enabled);
}

void SessionManager::setPowerProfile(SessionId id, PowerProfile profile) {
    std::lock_guard<std::mutex> lock(mu_);
    Session* session = sessionLocked(id);
    if (!session) {
        return;
    }
    session->setPowerProfile(profile);
}

Result SessionManager::punchNat(SessionId id) {
    std::lock_guard<std::mutex> lock(mu_);
    Session* session = sessionLocked(id);
    if (!session) {
        return Result::InvalidArg;
    }
    return session->punchUdpNat();
}

void SessionManager::setPreConnectHook(std::function<void()> hook) {
    std::lock_guard<std::mutex> lock(mu_);
    preConnectHook_ = std::move(hook);
}

} // namespace owalkie
