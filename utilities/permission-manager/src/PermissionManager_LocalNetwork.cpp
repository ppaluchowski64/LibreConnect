#include <PermissionManager.h>

#ifdef MACOS_DEVICE
#include <DebugLog.h>
#include <dns_sd.h>
#include <sys/select.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#endif

namespace {
#ifdef MACOS_DEVICE
using namespace std::chrono_literals;

constexpr std::chrono::milliseconds kPermissionFlowRetryDelay = 50ms;
constexpr std::chrono::milliseconds kPermissionProbePollInterval = 250ms;
constexpr std::chrono::seconds kPermissionProbeTimeout = 120s;
constexpr char kPermissionProbeServiceName[] = "LibreConnectPermissionProbe";
constexpr char kPermissionProbeServiceType[] = "_libreconnect._tcp";

std::atomic<bool> g_localNetworkPermissionGranted{false};
std::atomic<bool> g_localNetworkPermissionFlowInProgress{false};

enum class LocalNetworkPermissionProbeResult : uint8_t {
    Granted,
    Denied,
    Unknown
};

struct LocalNetworkPermissionProbeContext {
    std::atomic<LocalNetworkPermissionProbeResult> result{LocalNetworkPermissionProbeResult::Unknown};
    std::atomic<bool> completed{false};
};

class LocalNetworkPermissionFlowLock final {
public:
    explicit LocalNetworkPermissionFlowLock(const bool locked = false) noexcept : m_locked(locked) {}
    LocalNetworkPermissionFlowLock(LocalNetworkPermissionFlowLock&& other) noexcept : m_locked(other.m_locked) {
        other.m_locked = false;
    }
    LocalNetworkPermissionFlowLock& operator=(LocalNetworkPermissionFlowLock&& other) noexcept {
        if (this != &other) {
            Release();
            m_locked = other.m_locked;
            other.m_locked = false;
        }
        return *this;
    }

    LocalNetworkPermissionFlowLock(const LocalNetworkPermissionFlowLock&) = delete;
    LocalNetworkPermissionFlowLock& operator=(const LocalNetworkPermissionFlowLock&) = delete;

    ~LocalNetworkPermissionFlowLock() {
        Release();
    }

private:
    void Release() noexcept {
        if (m_locked) {
            g_localNetworkPermissionFlowInProgress.store(false, std::memory_order_release);
            m_locked = false;
        }
    }

    bool m_locked{false};
};

asio::awaitable<LocalNetworkPermissionFlowLock> AcquireLocalNetworkPermissionFlowLock() {
    const auto executor = co_await asio::this_coro::executor;
    asio::steady_timer waitTimer(executor);

    while (true) {
        bool expected = false;
        if (g_localNetworkPermissionFlowInProgress.compare_exchange_weak(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire
        )) {
            co_return LocalNetworkPermissionFlowLock(true);
        }

        waitTimer.expires_after(kPermissionFlowRetryDelay);
        asio::error_code ec;
        co_await waitTimer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    }
}

void DNSSD_API OnLocalNetworkPermissionServiceRegister(
    DNSServiceRef serviceRef,
    DNSServiceFlags flags,
    DNSServiceErrorType errorCode,
    const char* serviceName,
    const char* regtype,
    const char* domain,
    void* context) {
    (void)serviceRef;
    (void)flags;
    (void)serviceName;
    (void)regtype;
    (void)domain;

    auto* probeContext = static_cast<LocalNetworkPermissionProbeContext*>(context);
    if (probeContext == nullptr) {
        return;
    }

    if (errorCode == kDNSServiceErr_NoError) {
        probeContext->result.store(LocalNetworkPermissionProbeResult::Granted, std::memory_order_release);
    } else if (errorCode == kDNSServiceErr_PolicyDenied) {
        probeContext->result.store(LocalNetworkPermissionProbeResult::Denied, std::memory_order_release);
    } else {
        probeContext->result.store(LocalNetworkPermissionProbeResult::Unknown, std::memory_order_release);
    }

    probeContext->completed.store(true, std::memory_order_release);
}

LocalNetworkPermissionProbeResult ProbeLocalNetworkPermission() {
    DNSServiceRef serviceRef = nullptr;
    LocalNetworkPermissionProbeContext probeContext;

    const DNSServiceErrorType registerResult = DNSServiceRegister(
        &serviceRef,
        0,
        0,
        kPermissionProbeServiceName,
        kPermissionProbeServiceType,
        nullptr,
        nullptr,
        0,
        0,
        nullptr,
        OnLocalNetworkPermissionServiceRegister,
        &probeContext
    );

    if (registerResult == kDNSServiceErr_PolicyDenied) {
        return LocalNetworkPermissionProbeResult::Denied;
    }

    if (registerResult != kDNSServiceErr_NoError || serviceRef == nullptr) {
        Debug::LogWarning("Local network permission probe failed to start (DNS-SD error {})", registerResult);
        return LocalNetworkPermissionProbeResult::Unknown;
    }

    const int socketFd = DNSServiceRefSockFD(serviceRef);
    if (socketFd < 0) {
        Debug::LogWarning("Local network permission probe failed to get DNS-SD socket");
        DNSServiceRefDeallocate(serviceRef);
        return LocalNetworkPermissionProbeResult::Unknown;
    }

    const auto deadline = std::chrono::steady_clock::now() + kPermissionProbeTimeout;
    while (!probeContext.completed.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketFd, &readSet);

        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto waitDuration = std::min(remaining, kPermissionProbePollInterval);
        timeval timeout{};
        timeout.tv_sec = static_cast<time_t>(waitDuration.count() / 1000);
        timeout.tv_usec = static_cast<suseconds_t>((waitDuration.count() % 1000) * 1000);

        const int ready = select(socketFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(socketFd, &readSet)) {
            const DNSServiceErrorType processResult = DNSServiceProcessResult(serviceRef);
            if (processResult == kDNSServiceErr_PolicyDenied) {
                probeContext.result.store(LocalNetworkPermissionProbeResult::Denied, std::memory_order_release);
                probeContext.completed.store(true, std::memory_order_release);
                break;
            }

            if (processResult != kDNSServiceErr_NoError) {
                Debug::LogWarning("Local network permission probe failed while processing DNS-SD events ({})", processResult);
                probeContext.result.store(LocalNetworkPermissionProbeResult::Unknown, std::memory_order_release);
                probeContext.completed.store(true, std::memory_order_release);
                break;
            }
        } else if (ready < 0) {
            Debug::LogWarning("Local network permission probe select() failed");
            probeContext.result.store(LocalNetworkPermissionProbeResult::Unknown, std::memory_order_release);
            probeContext.completed.store(true, std::memory_order_release);
            break;
        }
    }

    const bool completed = probeContext.completed.load(std::memory_order_acquire);
    const LocalNetworkPermissionProbeResult result = completed
        ? probeContext.result.load(std::memory_order_acquire)
        : LocalNetworkPermissionProbeResult::Unknown;

    DNSServiceRefDeallocate(serviceRef);

    if (!completed) {
        Debug::LogWarning("Local network permission probe timed out waiting for user response");
    }

    return result;
}
#endif
}

asio::awaitable<bool> PermissionManager::RequestLocalNetworkAccessPermission() {
#ifdef MACOS_DEVICE
    if (g_localNetworkPermissionGranted.load(std::memory_order_acquire)) {
        co_return true;
    }

    auto flowLock = co_await AcquireLocalNetworkPermissionFlowLock();
    (void)flowLock;

    if (g_localNetworkPermissionGranted.load(std::memory_order_acquire)) {
        co_return true;
    }

    const LocalNetworkPermissionProbeResult result = ProbeLocalNetworkPermission();
    if (result == LocalNetworkPermissionProbeResult::Granted) {
        Debug::Log("Local network permission granted");
        g_localNetworkPermissionGranted.store(true, std::memory_order_release);
        co_return true;
    }

    if (result == LocalNetworkPermissionProbeResult::Denied) {
        Debug::LogWarning("Local network permission denied");
    } else {
        Debug::LogWarning("Local network permission state unresolved");
    }

    co_return false;
#else
    co_return true;
#endif
}

bool PermissionManager::IsLocalNetworkAccessPermissionGranted() {
#ifdef MACOS_DEVICE
    return g_localNetworkPermissionGranted.load(std::memory_order_acquire);
#else
    return true;
#endif
}
