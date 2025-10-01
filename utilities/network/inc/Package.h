#ifndef PACKAGE_H
#define PACKAGE_H

#ifndef NO_DISCARD
#define NO_DISCARD [[nodiscard]]
#endif

#include <DebugLog.h>
#include <type_traits>
#include <string>
#include <AsioCommon.h>
#include <Packable.h>
#include <boost/endian/conversion.hpp>
#include <tracy/Tracy.hpp>
#include <fmt/ostream.h>

enum class PackageFlag : uint8_t {
    NONE                  = 0,
    FILE_REQUEST          = 1 << 1,
    FILE_RECEIVE_INFO     = 1 << 2,
    REQUEST_WITH_RESPONSE = 1 << 3
};

inline uint8_t operator&(uint8_t l, PackageFlag r) {
    return l & static_cast<uint8_t>(r);
}

inline uint8_t operator&(PackageFlag l, uint8_t r) {
    return static_cast<uint8_t>(l) & r;
}

inline uint8_t operator|(uint8_t l, PackageFlag r) {
    return l | static_cast<uint8_t>(r);
}

inline uint8_t operator|(PackageFlag l, uint8_t r) {
    return static_cast<uint8_t>(l) | r;
}

inline uint8_t operator|(PackageFlag l, PackageFlag r) {
    return static_cast<uint8_t>(l) | static_cast<uint8_t>(r);
}

struct PackageHeader final {
    PackageTypeInt type{};
    PackageSizeInt size{};
    uint8_t        flags{};

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const{
        SerializeObject(type, buffer, offset);
        SerializeObject(size, buffer, offset);
        SerializeObject(flags, buffer, offset);
    }
    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
        DeserializeObject(type, buffer, offset);
        DeserializeObject(size, buffer, offset);
        DeserializeObject(flags, buffer, offset);
    }

    static constexpr size_t GetObjectSerializedSize() {
        return sizeof(type) + sizeof(size) + sizeof(flags);
    }
};

inline std::ostream& operator<<(std::ostream& os, const PackageHeader& object) {
    os << "Type: " << static_cast<size_t>(object.type) << ", Size: " << static_cast<size_t>(object.size) << ", Flags: " << static_cast<size_t>(object.flags);
    return os;
}

template <>
struct fmt::formatter<PackageHeader> : fmt::ostream_formatter {};

template <PackageType T>
class Package final {
public:
    Package() {
        m_header = {0, 0};
    }

    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;

    Package(Package&& other) noexcept : m_buffer(std::move(other.m_buffer)), m_header(other.m_header), m_readOffset(other.m_readOffset) {
        other.m_buffer.clear();
    }

    Package& operator=(Package&& other) noexcept {
        if (this != &other) {
            m_buffer = std::move(other.m_buffer);
            m_header = other.m_header;
            m_readOffset = other.m_readOffset;

            other.m_buffer.clear();
        }

        return *this;
    }

    ~Package() = default;

    explicit Package(const PackageHeader& header) : m_header(header) {
        m_buffer.resize(header.size);
    }

    NO_DISCARD PackageHeader& GetHeader() {
        return m_header;
    }

    NO_DISCARD PackageHeader GetHeaderCopy() const {
        return m_header;
    }

    NO_DISCARD uint8_t* GetRawBody() {
        return m_buffer.data();
    }

    template <Serializable T0>
    NO_DISCARD T0 GetValue() {
        T0 value{};

        if constexpr (Packable<T0>) {
            value.Deserialize(m_buffer, m_readOffset);
        } else {
            DeserializeObject(value, m_buffer, m_readOffset);
        }

        return value;
    }

    template <Serializable T0>
    void GetValue(T0& value) {
        if constexpr (Packable<T0>) {
            value.Deserialize(m_buffer, m_readOffset);
        } else {
            DeserializeObject(value, m_buffer, m_readOffset);
        }
    }

    template <Serializable... Args>
    static Package Create(T type, Args... args) {
        PackageHeader header {
            static_cast<PackageTypeInt>(type),
            0,
            0
        };

        (CalculateElementSize(args, header), ...);
        Package newPackage(header);
        size_t offset = 0;
        (InsertElementToBody(args, newPackage, offset), ...);

        return newPackage;
    }

    template <Serializable... Args>
    static std::unique_ptr<Package> CreateUnique(T type, Args... args) {
        PackageHeader header {
            static_cast<PackageTypeInt>(type),
            0,
            0
        };

        (CalculateElementSize(args, header), ...);
        std::unique_ptr<Package> newPackage = std::make_unique<Package>(header);
        size_t offset = 0;
        (InsertElementToBody(args, *newPackage, offset), ...);

        return newPackage;
    }



private:
    template <Serializable T0>
    static void InsertElementToBody(T0& arg, Package& package, size_t& offset) {
        if constexpr (Packable<T0>) {
            arg.Serialize(package.m_buffer, offset);
        } else {
            SerializeObject(arg, package.m_buffer, offset);
        }
    }

    template <Serializable T0>
    static void CalculateElementSize(const T0& arg, PackageHeader& header) {
        if constexpr (Packable<T0>) {
            header.size += arg.GetObjectSerializedSize();
        } else {
            header.size += GetObjectSerializedSize(arg);
        }
    }

    std::vector<uint8_t> m_buffer;
    PackageHeader        m_header{};
    size_t               m_readOffset{0};

};

#endif //PACKAGE_H