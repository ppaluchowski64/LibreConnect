#ifndef PACKABLE_H
#define PACKABLE_H

#include <concepts>
#include <vector>
#include <string>
#include <boost/endian/conversion.hpp>

template <typename T>
concept Primitive = std::integral<T> || std::floating_point<T>;

template <typename T>
concept Packable = requires(T a, std::vector<uint8_t>& buf, size_t& offset) {
    { a.Serialize(buf, offset) };
    { a.Deserialize(buf, offset) };
    { a.GetObjectSerializedSize() } -> std::convertible_to<size_t>;
};

template <Primitive T>
inline void SerializePrimitive(T object, std::vector<uint8_t>& buffer, size_t& offset) {
    boost::endian::native_to_big_inplace(object);
    memcpy(&buffer[offset], &object, sizeof(object));
    offset += sizeof(object);
}

template <Primitive T>
inline void DeserializePrimitive(T& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    memcpy(&object, &buffer[offset], sizeof(object));
    boost::endian::big_to_native_inplace(object);
    offset += sizeof(object);
}

inline void SerializeString(const std::string& object, std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size = object.size();
    boost::endian::native_to_big_inplace(size);

    memcpy(&buffer[offset], &size, sizeof(size));
    offset += sizeof(size);

    memcpy(&buffer[offset], object.c_str(), object.size());
    offset += object.size();
}

inline void DeserializeString(std::string& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    memcpy(&size, &buffer[offset], sizeof(size));
    boost::endian::big_to_native_inplace(size);
    offset += sizeof(size);

    object.resize(size);
    memcpy(&object[0], &buffer[offset], size);
    offset += size;
}

constexpr size_t GetStringSerializedSize(const std::string& object) {
    return sizeof(size_t) + object.size();
}

template <Primitive T>
inline void SerializeVector(const std::vector<T>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size = object.size();
    boost::endian::native_to_big_inplace(size);

    memcpy(&buffer[offset], &size, sizeof(size));
    offset += sizeof(size);

    for (const auto element : object) {
        SerializePrimitive(element, buffer, offset);
    }
}

template <Packable T>
inline void SerializeVector(const std::vector<T>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size = object.size();
    boost::endian::native_to_big_inplace(size);

    memcpy(&buffer[offset], &size, sizeof(size));
    offset += sizeof(size);

    for (const auto& element : object) {
        element.Serialize(buffer, offset);
    }
}

inline void SerializeVector(const std::vector<std::string>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size = object.size();
    boost::endian::native_to_big_inplace(size);

    memcpy(&buffer[offset], &size, sizeof(size));
    offset += sizeof(size);

    for (const auto& element : object) {
        SerializeString(element, buffer, offset);
    }
}

template <Primitive T>
inline void DeserializeVector(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    memcpy(&size, &buffer[offset], sizeof(size));
    boost::endian::big_to_native_inplace(size);
    offset += sizeof(size);
    object.resize(size);

    for (auto& element : object) {
        DeserializePrimitive(element, buffer, offset);
    }
}

template <Packable T>
inline void DeserializeVector(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    memcpy(&size, &buffer[offset], sizeof(size));
    boost::endian::big_to_native_inplace(size);
    offset += sizeof(size);
    object.resize(size);

    for (auto& element : object) {
        element.Deserialize(buffer, offset);
    }
}

inline void DeserializeVector(std::vector<std::string>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    memcpy(&size, &buffer[offset], sizeof(size));
    boost::endian::big_to_native_inplace(size);
    offset += sizeof(size);
    object.resize(size);

    for (auto& element : object) {
        DeserializeString(element, buffer, offset);
    }
}

template <Primitive T>
constexpr size_t GetVectorSerializedSize(const std::vector<T>& object) {
    return sizeof(size_t) + sizeof(T) * object.size();
}

template <Packable T>
constexpr size_t GetVectorSerializedSize(const std::vector<T>& object) {
    size_t result = sizeof(size_t);
    for (const auto& element : object) {
        result += element.GetObjectSerializedSize();
    }

    return result;
}

constexpr size_t GetVectorSerializedSize(const std::vector<std::string>& object) {
    size_t result = sizeof(size_t);
    for (const auto& element : object) {
        result += GetStringSerializedSize(element);
    }

    return result;
}


#endif //PACKABLE_H
