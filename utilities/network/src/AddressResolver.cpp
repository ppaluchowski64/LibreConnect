#include <AddressResolver.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <iphlpapi.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "iphlpapi.lib")
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <cstring>
  #include <cerrno>
#endif

#include <vector>
#include <string>
#include <set>

bool AddressResolver::IsAddressPublic(const IPAddress& address) {
    if (address.is_v4()) {
        return IsAddressPublic(address.to_v4());
    }

    if (address.is_v6()) {
        return IsAddressPublic(address.to_v6());
    }

    return false;
}

bool AddressResolver::IsAddressPublic(const asio::ip::address_v6& address) {
    return (address.to_bytes()[0] & 0xE0) == 0x20;
}

bool AddressResolver::IsAddressPublic(const asio::ip::address_v4& address) {
    if (IsAddressPrivate(address)) {
        return false;
    }

    const uint32_t ip = address.to_uint();
    const uint8_t b1 = (ip >> 24) & 0xFF;
    const uint8_t b2 = (ip >> 16) & 0xFF;

    if (b1 == 127)
        return false;

    if (b1 == 169 && b2 == 254)
        return false;

    if (b1 == 100 && (b2 >= 64 && b2 <= 127))
        return false;

    if (b1 == 192 && b2 == 0 && ((ip >> 8) & 0xFF) == 2)
        return false;
    if (b1 == 198 && b2 == 51 && ((ip >> 8) & 0xFF) == 100)
        return false;
    if (b1 == 203 && b2 == 0 && ((ip >> 8) & 0xFF) == 113)
        return false;

    if (b1 >= 224 && b1 <= 239)
        return false;

    if (b1 >= 240)
        return false;

    if (address == asio::ip::address_v4::broadcast() || address == asio::ip::address_v4::any())
        return false;

    return true;
}

bool AddressResolver::IsAddressPrivate(const IPAddress& address) {
    if (address.is_v4()) {
        return IsAddressPrivate(address.to_v4());
    }

    if (address.is_v6()) {
        return IsAddressPrivate(address.to_v6());
    }

    return false;
}

bool AddressResolver::IsAddressPrivate(const asio::ip::address_v6& address) {
    return (address.to_bytes()[0] & 0xFE) == 0xFC;
}

bool AddressResolver::IsAddressPrivate(const asio::ip::address_v4& address) {
    const uint32_t ip = address.to_uint();
    const uint8_t b1 = (ip >> 24) & 0xFF;
    const uint8_t b2 = (ip >> 16) & 0xFF;

    if (b1 == 10)
        return true;
    if (b1 == 172 && (b2 >= 16 && b2 <= 31))
        return true;
    if (b1 == 192 && b2 == 168)
        return true;

    return false;
}

static void push_unique(std::vector<IPAddress>& out, const IPAddress& a) {
    for (auto &x : out) if (x == a) return;
    out.push_back(a);
}

static std::vector<IPAddress> EnumerateAllAddresses() {
    std::vector<IPAddress> result;

#ifdef _WIN32
    ULONG outBufLen = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, nullptr, &outBufLen) != ERROR_BUFFER_OVERFLOW) {
        outBufLen = 16 * 1024;
    }

    std::vector<uint8_t> buffer;
    buffer.resize(outBufLen);
    IP_ADAPTER_ADDRESSES* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    DWORD rv = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &outBufLen);
    if (rv == ERROR_BUFFER_OVERFLOW) {
        buffer.resize(outBufLen);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        rv = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapters, &outBufLen);
    }

    if (rv != NO_ERROR) {
        Debug::LogError(std::string("GetAdaptersAddresses failed: ") + std::to_string((int)rv));
        return result;
    }

    for (IP_ADAPTER_ADDRESSES* adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp)
            continue;

        for (IP_ADAPTER_UNICAST_ADDRESS* ua = adapter->FirstUnicastAddress; ua; ua = ua->Next) {
            if (!ua->Address.lpSockaddr) continue;
            sockaddr* sa = ua->Address.lpSockaddr;
            char addrbuf[INET6_ADDRSTRLEN] = {0};

            if (sa->sa_family == AF_INET) {
                sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(sa);
                inet_ntop(AF_INET, &sin->sin_addr, addrbuf, sizeof(addrbuf));
                try {
                    push_unique(result, asio::ip::make_address(std::string(addrbuf)));
                } catch (...) { }
            } else if (sa->sa_family == AF_INET6) {
                sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(sa);
                inet_ntop(AF_INET6, &sin6->sin6_addr, addrbuf, sizeof(addrbuf));
                try {
                    push_unique(result, asio::ip::make_address(std::string(addrbuf)));
                } catch (...) { }
            }
        }
    }

#else // POSIX

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        Debug::LogError(std::string("getifaddrs failed: ") + std::strerror(errno));
        return result;
    }

    for (struct ifaddrs* ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || !ifa->ifa_name) continue;

        if (!(ifa->ifa_flags & IFF_UP)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        char addrbuf[INET6_ADDRSTRLEN] = {0};
        if (ifa->ifa_addr->sa_family == AF_INET) {
            sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
            if (inet_ntop(AF_INET, &sin->sin_addr, addrbuf, sizeof(addrbuf))) {
                try { push_unique(result, asio::ip::make_address(std::string(addrbuf))); }
                catch (...) {}
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(ifa->ifa_addr);
            if (inet_ntop(AF_INET6, &sin6->sin6_addr, addrbuf, sizeof(addrbuf))) {
                try { push_unique(result, asio::ip::make_address(std::string(addrbuf))); }
                catch (...) {}
            }
        }
    }

    freeifaddrs(ifaddr);

#endif

    return result;
}

std::vector<IPAddress> AddressResolver::GetAllIPv4() {
    const std::vector<IPAddress> all = EnumerateAllAddresses();
    std::vector<IPAddress> out;
    for (const auto& a : all) if (a.is_v4()) out.push_back(a);
    return out;
}

std::vector<IPAddress> AddressResolver::GetAllIPv6() {
    const std::vector<IPAddress> all = EnumerateAllAddresses();
    std::vector<IPAddress> out;
    for (const auto& a : all) if (a.is_v6()) out.push_back(a);
    return out;
}

std::vector<IPAddress> AddressResolver::GetAllPublicIPv4() {
    std::vector<IPAddress> allv4 = GetAllIPv4();
    std::vector<IPAddress> out;
    for (const auto& a : allv4) {
        if (IsAddressPublic(a.to_v4())) out.push_back(a);
    }
    return out;
}

std::vector<IPAddress> AddressResolver::GetAllPrivateIPv4() {
    std::vector<IPAddress> allv4 = GetAllIPv4();
    std::vector<IPAddress> out;
    for (const auto& a : allv4) {
        if (IsAddressPrivate(a.to_v4())) out.push_back(a);
    }
    return out;
}

std::vector<IPAddress> AddressResolver::GetAllPublicIPv6() {
    std::vector<IPAddress> allv6 = GetAllIPv6();
    std::vector<IPAddress> out;
    for (const auto& a : allv6) {
        if (IsAddressPublic(a.to_v6())) out.push_back(a);
    }
    return out;
}

std::vector<IPAddress> AddressResolver::GetAllPrivateIPv6() {
    std::vector<IPAddress> allv6 = GetAllIPv6();
    std::vector<IPAddress> out;
    for (const auto& a : allv6) {
        if (IsAddressPrivate(a.to_v6())) out.push_back(a);
    }
    return out;
}

std::vector<NetworkInterfaceData> AddressResolver::GetAllNetworkInterfaces() {
    std::vector<NetworkInterfaceData> result;

#ifdef _WIN32

    ULONG bufferSize = 0;
    GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, nullptr, &bufferSize);

    std::vector<uint8_t> buffer(bufferSize);
    IP_ADAPTER_ADDRESSES_LH* addresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

    if (GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, addresses, &bufferSize) != NO_ERROR) {
        Debug::LogError("Error retrieving adapters");
        return result;
    }

    for (const IP_ADAPTER_ADDRESSES_LH* adapter = addresses; adapter; adapter = adapter->Next)
    {
        if (!IsInterfaceValid(adapter))
            continue;

        NetworkInterfaceData data;

        char macStr[18];
        sprintf_s(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            adapter->PhysicalAddress[0],
            adapter->PhysicalAddress[1],
            adapter->PhysicalAddress[2],
            adapter->PhysicalAddress[3],
            adapter->PhysicalAddress[4],
            adapter->PhysicalAddress[5]);

        data.macAddress = std::string(macStr);

        for (const _IP_ADAPTER_UNICAST_ADDRESS_LH* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
        {
            if (unicast->Address.lpSockaddr->sa_family != AF_INET)
                continue;

            char addrStr[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(unicast->Address.lpSockaddr)->sin_addr, addrStr, sizeof(addrStr));
            data.ipAddress = asio::ip::make_address(addrStr);
        }

        result.push_back(data);
    }

#endif

    return result;
}

#ifdef _WIN32

bool AddressResolver::IsInterfaceValid(const void* adapterPtr) {
    const IP_ADAPTER_ADDRESSES* adapter = static_cast<const IP_ADAPTER_ADDRESSES*>(adapterPtr);

    if (adapter == nullptr) {
        return false;
    }

    if (adapter->OperStatus != IfOperStatusUp) {
        return false;
    }

    if (adapter->IfType != IF_TYPE_ETHERNET_CSMACD && adapter->IfType != IF_TYPE_IEEE80211) {
        return false;
    }

    return true;
}

#endif
