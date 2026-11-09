#pragma once
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <expected>

namespace nefarius::winapi::services::detail
{
enum class RemovalState { Removed, PendingDeletion };
// Injectable SCM/time operations; no real services or sleeps in regression tests.
class RemovalOperations
{
public:
    using Milliseconds = std::chrono::milliseconds;
    using TimePoint = std::chrono::steady_clock::time_point;
    virtual ~RemovalOperations() = default;
    virtual DWORD RequestDeletion(Milliseconds stopTimeout) = 0;
    // SUCCESS means present; DOES_NOT_EXIST alone proves absence.
    virtual DWORD ProbeRegistration() = 0;
    virtual TimePoint Now() = 0;
    virtual void Delay(Milliseconds duration) = 0;
};
inline std::expected<RemovalState, DWORD> RemoveAndWait(
    RemovalOperations& operations, std::chrono::milliseconds stopTimeout,
    std::chrono::milliseconds retryTimeout)
{
    using namespace std::chrono;
    const auto deadline = operations.Now() + std::max(stopTimeout, milliseconds::zero())
        + std::max(retryTimeout, milliseconds::zero());
    bool deletionAccepted = false;
    DWORD lastError = ERROR_SERVICE_REQUEST_TIMEOUT;
    for (;;)
    {
        if (!deletionAccepted)
        {
            const auto remaining = std::max(duration_cast<milliseconds>(deadline - operations.Now()),
                                            milliseconds::zero());
            lastError = operations.RequestDeletion(std::clamp(stopTimeout, milliseconds::zero(), remaining));
            if (lastError == ERROR_SERVICE_DOES_NOT_EXIST)
                return RemovalState::Removed;
            deletionAccepted = lastError == ERROR_SUCCESS || lastError == ERROR_SERVICE_MARKED_FOR_DELETE;
            if (!deletionAccepted && lastError != ERROR_SHARING_VIOLATION
                && lastError != ERROR_SERVICE_REQUEST_TIMEOUT && lastError != ERROR_SERVICE_CANNOT_ACCEPT_CTRL)
                return std::unexpected(lastError);
        }
        if (deletionAccepted)
        {
            // RequestDeletion has returned and closed its handles before this probe.
            const DWORD probe = operations.ProbeRegistration();
            if (probe == ERROR_SERVICE_DOES_NOT_EXIST)
                return RemovalState::Removed;
            if (probe != ERROR_SUCCESS && probe != ERROR_SERVICE_MARKED_FOR_DELETE)
                return std::unexpected(probe);
        }
        const auto remaining = duration_cast<milliseconds>(deadline - operations.Now());
        if (remaining <= milliseconds::zero())
        {
            if (deletionAccepted)
                return RemovalState::PendingDeletion;
            return std::unexpected(lastError);
        }
        operations.Delay(std::min(milliseconds(100), remaining));
        // Probe accepted deletion at the deadline; never start another blocking request there.
        if (!deletionAccepted && operations.Now() >= deadline)
            return std::unexpected(lastError);
    }
}
}
