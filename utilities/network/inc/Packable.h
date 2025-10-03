#ifndef PACKABLE_H
#define PACKABLE_H

#include <concepts>
#include <type_traits>
#include <vector>
#include <string>
#include <boost/endian/conversion.hpp>

template <typename T>
concept Primitive = std::integral<T> || std::floating_point<T>;

template <typename T>
concept Packable = requires(T a, std::vector<uint8_t>& buf, size_t& offset) {
    { a.Serialize(buf, offset) };
    { a.Deserialize(buf, offset) };
    { a.GetSerializedSize() } -> std::convertible_to<size_t>;
};

template <typename T>
struct is_packable_vector : std::false_type {};

template<typename T>
struct is_packable_vector<std::vector<T>> : std::bool_constant<Primitive<T> || Packable<T> || std::is_same_v<std::string, T>> {};

template <typename T>
concept Serializable =
    Primitive<T> ||
    Packable<T> ||
    std::is_same_v<T, std::string> ||
    is_packable_vector<T>::value;

template <Primitive T>
inline void SerializeObject(T object, std::vector<uint8_t>& buffer, size_t& offset) {
    boost::endian::native_to_big_inplace(object);
    std::memcpy(&buffer[offset], &object, sizeof(object));
    offset += sizeof(object);
}

template <Primitive T>
inline void DeserializeObject(T& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    std::memcpy(&object, &buffer[offset], sizeof(object));
    boost::endian::big_to_native_inplace(object);
    offset += sizeof(object);
}

template <Primitive T>
constexpr size_t GetObjectSerializedSize(T& object) {
    return sizeof(object);
}

inline void SerializeObject(const std::string& object, std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size = object.size();
    boost::endian::native_to_big_inplace(size);

    std::memcpy(&buffer[offset], &size, sizeof(size));
    offset += sizeof(size);

    std::memcpy(&buffer[offset], object.c_str(), object.size());
    offset += object.size();
}

inline void DeserializeObject(std::string& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    std::memcpy(&size, &buffer[offset], sizeof(size));
    boost::endian::big_to_native_inplace(size);
    offset += sizeof(size);

    object.resize(size);
    std::memcpy(&object[0], &buffer[offset], size);
    offset += size;
}

constexpr size_t GetObjectSerializedSize(const std::string& object) {
    return sizeof(size_t) + object.size();
}

template <Primitive T>
inline void SerializeObject(const std::vector<T>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size = object.size();
    boost::endian::native_to_big_inplace(size);

    std::memcpy(&buffer[offset], &size, sizeof(size));
    offset += sizeof(size);

    for (const auto element : object) {
        SerializeObject(element, buffer, offset);
    }
}

template <Packable T>
inline void SerializeObject(const std::vector<T>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size = object.size();
    boost::endian::native_to_big_inplace(size);

    std::memcpy(&buffer[offset], &size, sizeof(size));
    offset += sizeof(size);

    for (const auto& element : object) {
        element.Serialize(buffer, offset);
    }
}

inline void SerializeObject(const std::vector<std::string>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size = object.size();
    boost::endian::native_to_big_inplace(size);

    std::memcpy(&buffer[offset], &size, sizeof(size));
    offset += sizeof(size);

    for (const auto& element : object) {
        SerializeObject(element, buffer, offset);
    }
}

template <Primitive T>
inline void DeserializeObject(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    std::memcpy(&size, &buffer[offset], sizeof(size));
    boost::endian::big_to_native_inplace(size);
    offset += sizeof(size);
    object.resize(size);

    for (auto& element : object) {
        DeserializeObject(element, buffer, offset);
    }
}

template <Packable T>
inline void DeserializeObject(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    std::memcpy(&size, &buffer[offset], sizeof(size));
    boost::endian::big_to_native_inplace(size);
    offset += sizeof(size);
    object.resize(size);

    for (auto& element : object) {
        element.Deserialize(buffer, offset);
    }
}

inline void DeserializeObject(std::vector<std::string>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    std::memcpy(&size, &buffer[offset], sizeof(size));
    boost::endian::big_to_native_inplace(size);
    offset += sizeof(size);
    object.resize(size);

    for (auto& element : object) {
        DeserializeObject(element, buffer, offset);
    }
}

template <Primitive T>
constexpr size_t GetObjectSerializedSize(const std::vector<T>& object) {
    return sizeof(size_t) + sizeof(T) * object.size();
}

template <Packable T>
constexpr size_t GetObjectSerializedSize(const std::vector<T>& object) {
    size_t result = sizeof(size_t);
    for (const auto& element : object) {
        result += element.GetSerializedSize();
    }

    return result;
}

constexpr size_t GetObjectSerializedSize(const std::vector<std::string>& object) {
    size_t result = sizeof(size_t);
    for (const auto& element : object) {
        result += GetObjectSerializedSize(element);
    }

    return result;
}


#endif //PACKABLE_H
