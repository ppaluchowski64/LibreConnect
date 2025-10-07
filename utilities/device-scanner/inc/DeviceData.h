#ifndef DEVICE_DATA_H
#define DEVICE_DATA_H

#include <boost/uuid/uuid.hpp>

class DeviceData {
public:
    static boost::uuids::uuid GetDeviceUUID();
};

#endif //DEVICE_DATA_H
