#ifndef PACKABLE_H
#define PACKABLE_H

#include <concepts>
#include <type_traits>
#include <vector>
#include <string>
#include <boost/endian/conversion.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/string_generator.hpp>

template <typename T>
concept Primitive =
    std::integral<std::remove_cvref_t<T>> ||
    std::floating_point<std::remove_cvref_t<T>> ||
    std::is_enum_v<std::remove_cvref_t<T>> &&
    std::integral<std::underlying_type_t<std::remove_cvref_t<T>>>;

template <typename T>
concept Packable = requires(T a, std::vector<uint8_t>& buf, size_t& offset) {
    { a.Serialize(buf, offset) };
    { a.Deserialize(buf, offset) };
    { a.GetSerializedSize() } -> std::convertible_to<size_t>;
};

template <typename T>
concept SerializableValue =
    Primitive<T> ||
    Packable<T> ||
    std::is_same_v<boost::uuids::uuid, T> ||
    std::is_same_v<std::string, T>;

template <typename T>
struct is_packable_vector : std::false_type {};

template<typename T>
struct is_packable_vector<std::vector<T>> : std::bool_constant<SerializableValue<T>> {};

template <typename T>
concept Serializable =
    SerializableValue<T> ||
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
    const size_t size = object.size();
    SerializeObject(size, buffer, offset);

    std::memcpy(&buffer[offset], object.c_str(), object.size());
    offset += object.size();
}

inline void DeserializeObject(std::string& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    DeserializeObject(size, buffer, offset);

    object.resize(size);
    std::memcpy(&object[0], &buffer[offset], size);
    offset += size;
}

inline size_t GetObjectSerializedSize(const std::string& object) {
    return sizeof(size_t) + object.size();
}

template <Primitive T>
inline void SerializeObject(const std::vector<T>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    const size_t size = object.size();
    SerializeObject(size, buffer, offset);

    for (const auto element : object) {
        SerializeObject(element, buffer, offset);
    }
}

template <Packable T>
inline void SerializeObject(const std::vector<T>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    const size_t size = object.size();
    SerializeObject(size, buffer, offset);

    for (const auto& element : object) {
        element.Serialize(buffer, offset);
    }
}

inline void SerializeObject(const std::vector<std::string>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    const size_t size = object.size();
    SerializeObject(size, buffer, offset);

    for (const auto& element : object) {
        SerializeObject(element, buffer, offset);
    }
}

inline void SerializeObject(const boost::uuids::uuid& object, std::vector<uint8_t>& buffer, size_t& offset) {
    const std::string uuid = boost::uuids::to_string(object);
    SerializeObject(uuid, buffer, offset);
}

inline void DeserializeObject(boost::uuids::uuid& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    std::string uuid;
    DeserializeObject(uuid, buffer, offset);

    static boost::uuids::string_generator generator;
    object = generator(uuid);
}

template <Primitive T>
inline void DeserializeObject(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    DeserializeObject(size, buffer, offset);
    object.resize(size);

    for (auto& element : object) {
        DeserializeObject(element, buffer, offset);
    }
}

template <Packable T>
inline void DeserializeObject(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    DeserializeObject(size, buffer, offset);
    object.resize(size);

    for (auto& element : object) {
        element.Deserialize(buffer, offset);
    }
}

inline void DeserializeObject(std::vector<std::string>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    DeserializeObject(size, buffer, offset);
    object.resize(size);

    for (auto& element : object) {
        DeserializeObject(element, buffer, offset);
    }
}

template <Primitive T>
inline size_t GetObjectSerializedSize(const std::vector<T>& object) {
    return sizeof(size_t) + sizeof(T) * object.size();
}

template <Packable T>
inline size_t GetObjectSerializedSize(const std::vector<T>& object) {
    size_t result = sizeof(size_t);
    for (const auto& element : object) {
        result += element.GetSerializedSize();
    }

    return result;
}

inline size_t GetObjectSerializedSize(const std::vector<std::string>& object) {
    size_t result = sizeof(size_t);
    for (const auto& element : object) {
        result += GetObjectSerializedSize(element);
    }

    return result;
}

constexpr size_t GetObjectSerializedSize(const boost::uuids::uuid&) {
    return sizeof(size_t) + 36; // UUID string size
}

#endif //PACKABLE_H
