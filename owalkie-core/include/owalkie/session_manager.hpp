#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

#include "owalkie/session.hpp"

namespace owalkie {

using SessionId = uint64_t;
inline constexpr SessionId kInvalidSessionId = 0;
inline constexpr std::size_t kMaxManagedSessions = 16;

/** Multichannel session registry: connect(params, callbacks) → id; disconnect(id) destroys. */
class SessionManager {
public:
    static SessionManager& instance();

    SessionId connect(
        ConnectParams params,
        SessionCallbacks callbacks,
        std::function<void(SessionId id)> onAllocated = nullptr);
    void disconnect(SessionId id);
    /// Halt transport without emitting session events (process exit / destructor).
    void haltAndRelease(SessionId id);
    void disconnectAll();
    /// Halt all sessions, then wait up to @p timeoutMs for background teardown (process exit).
    void disconnectAllAndWait(int timeoutMs = 3000);
    /// Wait for background session teardown threads started by disconnect().
    void waitForPendingCleanups(int timeoutMs = 3000);

    bool isValid(SessionId id) const;
    bool isSessionReady(SessionId id) const;

    Result sendTxOpus(SessionId id, std::span<const uint8_t> opus, int signalStrength);
    Result sendTxEofBurst(SessionId id);
    Result setRepeaterMode(SessionId id, bool enabled);
    Result setTxSignalStrength(SessionId id, int strength);
    int txSignalStrength(SessionId id) const;
    void setPowerProfile(SessionId id, PowerProfile profile);
    void notifyNetworkChanged(SessionId id);
    Result resetUdpTransport(SessionId id);

private:
    SessionManager() = default;

    struct ManagedSession {
        SessionId id = kInvalidSessionId;
        std::unique_ptr<Session> session;
    };

    Session* sessionLocked(SessionId id) const;
    SessionCallbacks wrapCallbacks(SessionId id, SessionCallbacks user);
    void releaseSession(SessionId id, bool callDisconnect);
    void runConnect(SessionId id, ConnectParams params);
    void scheduleSessionCleanup(std::unique_ptr<ManagedSession> owned);
    void waitForCleanups(int timeoutMs);
    void pruneJoinedCleanups();

    mutable std::mutex mu_;
    std::unordered_map<SessionId, std::unique_ptr<ManagedSession>> sessions_;
    SessionId nextId_ = 1;

    std::mutex cleanupMu_;
    std::vector<std::thread> cleanupThreads_;
    std::atomic<int> pendingCleanups_{0};
};

} // namespace owalkie
