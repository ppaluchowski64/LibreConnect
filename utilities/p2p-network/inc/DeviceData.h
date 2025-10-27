#ifndef DEVICE_DATA_H
#define DEVICE_DATA_H

#include <mutex>
#include <vector>
#include <boost/uuid/uuid.hpp>
#include <SQLiteCpp/SQLiteCpp.h>

struct DeviceInfo;

class DeviceData {
public:
    static boost::uuids::uuid GetDeviceUUID();
    static bool IsDevicePaired(boost::uuids::uuid uuid);
    static void AddPairedDevice(const DeviceInfo& data);
    static void RemovePairedDevice(boost::uuids::uuid uuid);
    static std::vector<DeviceInfo> GetPairedDevices();
private:
    static void Init();

    static std::mutex m_mutex;
    static SQLite::Database m_dataBase;
    static std::once_flag m_initFlag;
};

#endif //DEVICE_DATA_H
