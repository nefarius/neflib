#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/ServiceRemovalInternal.hpp"
#include <cstdio>
#include <vector>

namespace
{
using namespace nefarius::winapi::services::detail;
using namespace std::chrono_literals;
class FakeScm final : public RemovalOperations
{
public:
    std::vector<DWORD> requests{ERROR_SUCCESS};
    std::vector<DWORD> probes{ERROR_SERVICE_DOES_NOT_EXIST};
    size_t requestCount = 0, probeCount = 0;
    TimePoint now{};
    Milliseconds requestDuration{};
    DWORD RequestDeletion(Milliseconds timeout) override
    {
        now += std::min(timeout, requestDuration);
        return requests[std::min(requestCount++, requests.size() - 1)];
    }
    DWORD ProbeRegistration() override { return probes[std::min(probeCount++, probes.size() - 1)]; }
    TimePoint Now() override { return now; }
    void Delay(Milliseconds duration) override { now += duration; }
};
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #condition); ++failures; } } while (false)

void ImmediateAndAbsent()
{
    FakeScm scm;
    auto result = RemoveAndWait(scm, 10ms, 20ms);
    CHECK(result && *result == RemovalState::Removed);
    CHECK(scm.requestCount == 1 && scm.probeCount == 1);
    scm = FakeScm{};
    scm.requests = {ERROR_SERVICE_DOES_NOT_EXIST};
    result = RemoveAndWait(scm, 10ms, 20ms);
    CHECK(result && *result == RemovalState::Removed);
    CHECK(scm.probeCount == 0);
}
void AcceptedDeletionMustSettle()
{
    for (DWORD present : {ERROR_SUCCESS, ERROR_SERVICE_MARKED_FOR_DELETE})
    {
        FakeScm scm;
        scm.probes = {present, present, ERROR_SERVICE_DOES_NOT_EXIST};
        const auto result = RemoveAndWait(scm, 100ms, 200ms);
        CHECK(result && *result == RemovalState::Removed);
        CHECK(scm.requestCount == 1 && scm.probeCount == 3);
    }
}
void PersistentDeletionAndFinalProbe()
{
    for (DWORD accepted : {ERROR_SUCCESS, ERROR_SERVICE_MARKED_FOR_DELETE})
    {
        FakeScm scm;
        scm.requests = {accepted};
        scm.probes = {ERROR_SUCCESS};
        const auto result = RemoveAndWait(scm, 100ms, 100ms);
        CHECK(result && *result == RemovalState::PendingDeletion);
        CHECK(scm.requestCount == 1 && scm.now == RemovalOperations::TimePoint{} + 200ms);
    }
    FakeScm scm;
    scm.probes = {ERROR_SUCCESS, ERROR_SUCCESS, ERROR_SERVICE_DOES_NOT_EXIST};
    const auto result = RemoveAndWait(scm, 100ms, 100ms);
    CHECK(result && *result == RemovalState::Removed);
}
void ErrorsAreNotReboots()
{
    FakeScm scm;
    scm.requests = {ERROR_ACCESS_DENIED};
    auto result = RemoveAndWait(scm, 100ms, 100ms);
    CHECK(!result && result.error() == ERROR_ACCESS_DENIED);
    CHECK(scm.probeCount == 0);
    scm = FakeScm{};
    scm.probes = {ERROR_ACCESS_DENIED};
    result = RemoveAndWait(scm, 100ms, 100ms);
    CHECK(!result && result.error() == ERROR_ACCESS_DENIED);
}
void RetryAndBudget()
{
    for (DWORD transient : {ERROR_SHARING_VIOLATION, ERROR_SERVICE_REQUEST_TIMEOUT, ERROR_SERVICE_CANNOT_ACCEPT_CTRL})
    {
        FakeScm scm;
        scm.requests = {transient, ERROR_SUCCESS};
        auto result = RemoveAndWait(scm, 100ms, 200ms);
        CHECK(result && *result == RemovalState::Removed);
        CHECK(scm.requestCount == 2);
        scm = FakeScm{};
        scm.requests = {transient};
        scm.requestDuration = 100ms;
        result = RemoveAndWait(scm, 100ms, 150ms);
        CHECK(!result && result.error() == transient);
        CHECK(scm.now == RemovalOperations::TimePoint{} + 250ms);
    }
}
void ZeroBudget()
{
    FakeScm scm;
    scm.probes = {ERROR_SERVICE_MARKED_FOR_DELETE};
    const auto result = RemoveAndWait(scm, 0ms, 0ms);
    CHECK(result && *result == RemovalState::PendingDeletion);
    CHECK(scm.requestCount == 1 && scm.probeCount == 1);
}
}
int RunServiceRemovalTests()
{
    ImmediateAndAbsent();
    AcceptedDeletionMustSettle();
    PersistentDeletionAndFinalProbe();
    ErrorsAreNotReboots();
    RetryAndBudget();
    ZeroBudget();
    std::printf("Service removal tests: %d failure(s).\n", failures);
    return failures;
}
