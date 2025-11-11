#ifndef ADDRESS_RESOLVER_H
#define ADDRESS_RESOLVER_H

#include <AsioCommon.h>
#include <DebugLog.h>

struct NetworkInterfaceData {
    std::string macAddress;
    IPAddress   ipAddress;
};

class AddressResolver final {
public:
    static bool IsAddressPublic(const asio::ip::address_v6& address);
    static bool IsAddressPublic(const asio::ip::address_v4& address);
    static bool IsAddressPublic(const IPAddress& address);

    static bool IsAddressPrivate(const asio::ip::address_v6& address);
    static bool IsAddressPrivate(const asio::ip::address_v4& address);
    static bool IsAddressPrivate(const IPAddress& address);

    static std::vector<IPAddress> GetAllIPv4();
    static std::vector<IPAddress> GetAllIPv6();

    static std::vector<IPAddress> GetAllPrivateIPv4();
    static std::vector<IPAddress> GetAllPrivateIPv6();
    static std::vector<IPAddress> GetAllPublicIPv4();
    static std::vector<IPAddress> GetAllPublicIPv6();

    static std::vector<NetworkInterfaceData> GetAllNetworkInterfaces();

private:
#ifdef _WIN32
    static bool IsInterfaceValid(const void* adapterPtr);
#endif
};

#endif //ADDRESS_RESOLVER_H
