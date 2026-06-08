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

/** Multichannel session registry: prepareConnection → id; connect(id) activates transport. */
class SessionManager {
public:
    static SessionManager& instance();

    SessionId prepareConnection(
        ConnectParams params,
        SessionCallbacks callbacks,
        std::function<void(SessionId id)> onAllocated = nullptr);
    /**
     * Single connect attempt (teardown + TCP/WS). Clients call while @p userIntent remains true.
     * @p timeoutMs TCP connect budget; 0 = default (~3.5s).
     */
    Result connect(SessionId id, int timeoutMs = 0);
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
    Result getSessionInfo(SessionId id, SessionState* out_state, bool* out_ready) const;

    Result submitTx(
        SessionId id,
        TxSubmitOp op,
        std::span<const int16_t> pcm = {},
        std::span<const uint8_t> opus = {});
    bool waitTxQueueIdle(SessionId id, int timeoutMs);
    Result setRepeaterMode(SessionId id, bool enabled);
    void setPowerProfile(SessionId id, PowerProfile profile);
    Result punchNat(SessionId id);
    /** Platform hook invoked before each blocking connect (e.g. Android network bind). */
    void setPreConnectHook(std::function<void()> hook);

private:
    SessionManager() = default;

    struct ManagedSession {
        SessionId id = kInvalidSessionId;
        std::unique_ptr<Session> session;
        ConnectParams params{};
        std::atomic<bool> userIntent{true};
        std::mutex sessionOpMu;
    };

    Session* sessionLocked(SessionId id) const;
    SessionCallbacks wrapCallbacks(SessionId id, SessionCallbacks user);
    void releaseSession(SessionId id, bool callDisconnect);
    void scheduleSessionCleanup(std::shared_ptr<ManagedSession> owned);
    void waitForCleanups(int timeoutMs);
    void pruneJoinedCleanups();
    static void signalUserDisconnect(const std::shared_ptr<ManagedSession>& managed);
    bool takeSessionForTeardown(
        SessionId id,
        int timeoutMs,
        std::shared_ptr<ManagedSession>& out,
        std::unique_lock<std::mutex>& opLockOut);

    mutable std::mutex mu_;
    std::unordered_map<SessionId, std::shared_ptr<ManagedSession>> sessions_;
    SessionId nextId_ = 1;

    std::mutex cleanupMu_;
    std::vector<std::thread> cleanupThreads_;
    std::atomic<int> pendingCleanups_{0};
    std::function<void()> preConnectHook_;
};

} // namespace owalkie
