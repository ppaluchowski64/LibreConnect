#include <AddressResolver.h>

#ifdef _WIN32

#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#endif

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

IPAddress AddressResolver::GetPrivateIPv4() {
    try {
        asio::io_context ioContext;
        asio::ip::tcp::resolver resolver(ioContext);

        const std::string hostname = asio::ip::host_name();

        for (const asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(hostname, ""); const auto& entry : endpoints) {
            if (const IPAddress entryAddress = entry.endpoint().address(); entryAddress.is_v4() && IsAddressPrivate(entryAddress)) {
                return entryAddress;
            }
        }
    } catch (const std::exception& e) {
        Debug::LogError(e.what());
        return {};
    }

    Debug::LogError("No address found");
    return {};
}

std::vector<IPAddress> AddressResolver::GetAllPrivateIPv4() {
    std::vector<IPAddress> addresses;

    try {
        asio::io_context ioContext;
        asio::ip::tcp::resolver resolver(ioContext);

        const std::string hostname = asio::ip::host_name();

        for (const asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(hostname, ""); const auto& entry : endpoints) {
            if (const IPAddress entryAddress = entry.endpoint().address(); entryAddress.is_v4() && IsAddressPrivate(entryAddress)) {
                addresses.push_back(entryAddress);
            }
        }
    } catch (const std::exception& e) {
        Debug::LogError(e.what());
        return {};
    }

    return addresses;
}

IPAddress AddressResolver::GetPrivateIPv6() {
    try {
        asio::io_context ctx;
        asio::ip::tcp::resolver resolver(ctx);

        const std::string hostname = asio::ip::host_name();

        for (const asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(hostname, ""); const auto& entry : endpoints) {
            if (const IPAddress entryAddress = entry.endpoint().address(); entryAddress.is_v6() && IsAddressPrivate(entryAddress)) {
                return entryAddress;
            }
        }
    } catch (const std::exception& e) {
        Debug::LogError(e.what());
        return {};
    }

    Debug::LogError("No address found");
    return {};
}

std::vector<IPAddress> AddressResolver::GetAllPrivateIPv6() {
    std::vector<IPAddress> addresses;
    try {
        asio::io_context ctx;
        asio::ip::tcp::resolver resolver(ctx);

        const std::string hostname = asio::ip::host_name();

        for (const asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(hostname, ""); const auto& entry : endpoints) {
            if (const IPAddress entryAddress = entry.endpoint().address(); entryAddress.is_v6() && IsAddressPrivate(entryAddress)) {
                addresses.push_back(entryAddress);
            }
        }
    } catch (const std::exception& e) {
        Debug::LogError(e.what());
        return {};
    }

    return addresses;
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
