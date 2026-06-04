#include "owalkie/session_manager.hpp"

#include "owalkie/protocol.hpp"
#include "owalkie/udp.hpp"

#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace owalkie {
namespace {

bool isTerminalEvent(EventType type) {
    return type == EventType::ConnectFailed ||
        type == EventType::Disconnected ||
        type == EventType::ProtocolError;
}

} // namespace

SessionManager& SessionManager::instance() {
    static SessionManager manager;
    return manager;
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
    wrapped.onSessionEvent = [this, id, userEvent](const Event& event) {
        if (userEvent) {
            userEvent(event);
        }
        if (isTerminalEvent(event.type)) {
            std::thread([this, id]() { releaseSession(id, false); }).detach();
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

void SessionManager::scheduleSessionCleanup(std::unique_ptr<ManagedSession> owned) {
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
    std::unique_ptr<ManagedSession> owned;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }
        owned = std::move(it->second);
        sessions_.erase(it);
    }
    if (!owned || !owned->session) {
        return;
    }
    (void)callDisconnect;
    owned->session->setCallbacks({});
    owned->session->disconnect();
    scheduleSessionCleanup(std::move(owned));
}

void SessionManager::runConnect(SessionId id, ConnectParams params) {
    Session* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        session = sessionLocked(id);
    }
    if (!session) {
        return;
    }

    const Result result = session->connect(params);
    if (result != Result::Ok) {
        std::lock_guard<std::mutex> lock(mu_);
        if (sessions_.find(id) != sessions_.end()) {
            std::thread([this, id]() { releaseSession(id, false); }).detach();
        }
    }
}

SessionId SessionManager::connect(
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
        auto managed = std::make_unique<ManagedSession>();
        managed->id = id;
        managed->session = std::move(session);
        managed->session->setCallbacks(wrapCallbacks(id, std::move(callbacks)));
        sessions_.emplace(id, std::move(managed));
    }

    if (onAllocated) {
        onAllocated(id);
    }

    std::thread([this, id, params = std::move(params)]() mutable {
        runConnect(id, std::move(params));
    }).detach();

    return id;
}

void SessionManager::disconnect(SessionId id) {
    std::unique_ptr<ManagedSession> owned;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }
        owned = std::move(it->second);
        sessions_.erase(it);
    }
    if (!owned || !owned->session) {
        return;
    }
    owned->session->disconnect();
    scheduleSessionCleanup(std::move(owned));
}

void SessionManager::haltAndRelease(SessionId id) {
    std::unique_ptr<ManagedSession> owned;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = sessions_.find(id);
        if (it == sessions_.end()) {
            return;
        }
        owned = std::move(it->second);
        sessions_.erase(it);
    }
    if (!owned || !owned->session) {
        return;
    }
    owned->session->setCallbacks({});
    owned->session->haltTransport();
    scheduleSessionCleanup(std::move(owned));
}

void SessionManager::disconnectAll() {
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

Result SessionManager::txStart(SessionId id) {
    std::lock_guard<std::mutex> lock(mu_);
    Session* session = sessionLocked(id);
    if (!session) {
        return Result::InvalidArg;
    }
    if (!session->isSessionReady()) {
        return Result::NotReady;
    }
    return session->txStart();
}

Result SessionManager::pushTxPcm(SessionId id, std::span<const int16_t> samples) {
    Session* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu_);
        session = sessionLocked(id);
        if (!session) {
            return Result::InvalidArg;
        }
    }
    if (!session->isSessionReady()) {
        return Result::NotReady;
    }
    return session->pushTxPcm(samples);
}

Result SessionManager::txEnd(SessionId id) {
    std::lock_guard<std::mutex> lock(mu_);
    Session* session = sessionLocked(id);
    if (!session) {
        return Result::InvalidArg;
    }
    return session->txEnd();
}

Result SessionManager::setRepeaterMode(SessionId id, bool enabled) {
    std::lock_guard<std::mutex> lock(mu_);
    Session* session = sessionLocked(id);
    if (!session) {
        return Result::InvalidArg;
    }
    return session->setRepeaterMode(enabled);
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

} // namespace owalkie
