#ifndef ASIO_COMMON_H
#define ASIO_COMMON_H

#include <type_traits>
#include <string>
#include <vector>
#include <asio.hpp>
#include <unordered_set>
#include <asio/ssl.hpp>
#include <asio/ssl/error.hpp>
#include <system_error>
#include <DebugLog.h>

typedef uint32_t PackageSizeInt;
typedef uint16_t PackageTypeInt;

typedef asio::io_context IOContext;
typedef asio::executor_work_guard<asio::io_context::executor_type> IOWorkGuard;
typedef asio::ssl::context SSLContext;
typedef asio::ip::tcp::socket TCPSocket;
typedef asio::ip::tcp::resolver TCPResolver;
typedef asio::ssl::stream<asio::ip::tcp::socket> SSLSocket;
typedef asio::ip::tcp::endpoint TCPEndpoint;
typedef asio::ip::tcp::acceptor TCPAcceptor;
typedef asio::ssl::context::method SSLMethod;
typedef asio::ssl::stream_base SSLStreamBase;
typedef asio::ip::address IPAddress;
typedef asio::ip::udp::endpoint UDPEndpoint;
typedef asio::ip::udp::resolver UDPResolver;
typedef asio::ip::udp::socket UDPSocket;
typedef asio::io_context::strand IOContextStrand;

typedef std::function<void(bool)> ConnectionCallbackType;
typedef std::function<void(std::string, uint16_t)> SeekReadyCallbackType;
typedef std::function<void()> DisconnectionCallbackType;

constexpr PackageSizeInt MAX_NON_FILE_PACKAGE_SIZE = 1024 * 32;
constexpr PackageSizeInt MAX_FULL_PACKAGE_SIZE = 1024 * 64;
constexpr PackageSizeInt MAX_FILE_NAME_SIZE = 255;
constexpr PackageSizeInt FILE_BUFFER_SIZE = 128 * 1024;
constexpr uint32_t PACKAGES_WARN_THRESHOLD = 10000;
constexpr uint32_t MAX_NUMBER_OF_VERIFICATION_TRIES = 5;

const asio::ip::address_v4 DEVICE_DISCOVERY_MULTICAST_ADDRESS = asio::ip::make_address_v4("239.255.123.242");
constexpr uint16_t DEVICE_DISCOVERY_MULTICAST_PORT            = 5430;

template <typename T>
concept PackageTypeConcept = std::is_same_v<std::underlying_type_t<T>, PackageTypeInt>;

template <typename T>
concept StandardLayaut = std::is_standard_layout_v<T>;

template<typename T>
struct is_std_layout_vector : std::false_type {};

template<typename U>
struct is_std_layout_vector<std::vector<U>>
    : std::bool_constant<std::is_standard_layout_v<U>> {};

template<typename T>
concept StdLayoutOrVecOrString =
    std::is_standard_layout_v<T> ||
    is_std_layout_vector<T>::value ||
    std::is_same_v<T, std::string>;

enum class ConnectionState : uint8_t {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

inline const std::unordered_set<std::error_code> g_asioNonFatalErrors {
    asio::error::eof,
    asio::error::connection_reset,
    asio::error::connection_aborted,
    asio::error::operation_aborted,
    asio::error::shut_down,
    asio::error::not_connected,
    asio::error::bad_descriptor,
    asio::ssl::error::stream_truncated,
    asio::ssl::error::unspecified_system_error,
    asio::ssl::error::stream_errors::stream_truncated
};

inline const std::unordered_set<std::error_code> g_asioWarningErrors{
    asio::error::broken_pipe,
    asio::error::timed_out,
    asio::error::no_buffer_space,
    asio::error::try_again,
    asio::error::would_block
};

inline bool IsNonFatalAsioError(const std::error_code& ec)
{
    return g_asioNonFatalErrors.contains(ec);
}

inline bool IsWarningAsioError(const std::error_code& ec)
{
    return g_asioWarningErrors.contains(ec);
}

inline void HandleAsioError(const std::error_code& ec)
{
    if (!ec)
        Debug::Log("Connection closed cleanly");
        return;

    if (IsNonFatalAsioError(ec)) {
        Debug::Log("Connection closed: {}", ec.message());
        return;
    }

    if (IsWarningAsioError(ec)) {
        Debug::LogWarning("Connection issue: {}", ec.message());
        return;
    }

    Debug::LogError("Fatal connection error: {}", ec.message());
}

inline bool PackageTypeIntHasFlag(const PackageTypeInt type, const PackageTypeInt flag) {
    return (type & flag) != 0;
}

#endif //ASIO_COMMON_H
