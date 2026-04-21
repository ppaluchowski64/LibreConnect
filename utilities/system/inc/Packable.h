#ifndef PACKABLE_H
#define PACKABLE_H

#include <optional>
#include <concepts>
#include <type_traits>
#include <vector>
#include <string>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <boost/endian/conversion.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/string_generator.hpp>

typedef boost::uuids::uuid uuid;


template <typename T>
struct is_pair : std::false_type {};
template <typename T, typename U>
struct is_pair<std::pair<T, U>> : std::true_type {};

template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};


template <typename T>
concept Primitive =
    std::integral<std::remove_cvref_t<T>> ||
    std::floating_point<std::remove_cvref_t<T>> ||
    (std::is_enum_v<std::remove_cvref_t<T>> &&
     std::integral<std::underlying_type_t<std::remove_cvref_t<T>>>);

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
    is_pair<std::remove_cvref_t<T>>::value ||
    is_optional<std::remove_cvref_t<T>>::value ||
    std::is_same_v<uuid, std::remove_cvref_t<T>> ||
    std::is_same_v<std::string, std::remove_cvref_t<T>>;

template <typename T>
struct is_packable_vector : std::false_type {};

template<typename T>
struct is_packable_vector<std::vector<T>> : std::bool_constant<SerializableValue<T>> {};

template <typename T>
concept Serializable =
    SerializableValue<T> ||
    is_packable_vector<T>::value;

template <Primitive T>
inline void SerializeObject(T object, std::vector<uint8_t>& buffer, size_t& offset);
template <Primitive T>
inline void DeserializeObject(T& object, const std::vector<uint8_t>& buffer, size_t& offset);
template <Primitive T>
constexpr size_t GetObjectSerializedSize(const T& object);

inline void SerializeObject(const std::string& object, std::vector<uint8_t>& buffer, size_t& offset);
inline void DeserializeObject(std::string& object, const std::vector<uint8_t>& buffer, size_t& offset);
inline size_t GetObjectSerializedSize(const std::string& object);

inline void SerializeObject(const uuid& object, std::vector<uint8_t>& buffer, size_t& offset);
inline void DeserializeObject(uuid& object, const std::vector<uint8_t>& buffer, size_t& offset);
inline size_t GetObjectSerializedSize(const uuid& object);

template <Packable T>
inline void SerializeObject(const T& object, std::vector<uint8_t>& buffer, size_t& offset);
template <Packable T>
inline void DeserializeObject(T& object, const std::vector<uint8_t>& buffer, size_t& offset);
template <Packable T>
inline size_t GetObjectSerializedSize(const T& object);

template <typename T1, typename T2>
inline void SerializeObject(const std::pair<T1, T2>& object, std::vector<uint8_t>& buffer, size_t& offset);
template <typename T1, typename T2>
inline void DeserializeObject(std::pair<T1, T2>& object, const std::vector<uint8_t>& buffer, size_t& offset);
template <typename T1, typename T2>
inline size_t GetObjectSerializedSize(const std::pair<T1, T2>& object);

template <typename T>
inline void SerializeObject(const std::optional<T>& opt, std::vector<uint8_t>& buffer, size_t& offset);
template <typename T>
inline void DeserializeObject(std::optional<T>& opt, const std::vector<uint8_t>& buffer, size_t& offset);
template <typename T>
inline size_t GetObjectSerializedSize(const std::optional<T>& opt);

template <typename T>
inline void SerializeObject(const std::vector<T>& object, std::vector<uint8_t>& buffer, size_t& offset);
template <typename T>
inline void DeserializeObject(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset);
template <typename T>
inline size_t GetObjectSerializedSize(const std::vector<T>& object);

template <Primitive T>
inline void SerializeObject(T object, std::vector<uint8_t>& buffer, size_t& offset) {
    boost::endian::native_to_big_inplace(object);
    std::memcpy(&buffer[offset], &object, sizeof(object));
    offset += sizeof(object);
}

template <Primitive T>
inline void DeserializeObject(T& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    if (offset > buffer.size() || sizeof(object) > buffer.size() - offset) {
        throw std::runtime_error("DeserializeObject primitive out of bounds");
    }
    std::memcpy(&object, &buffer[offset], sizeof(object));
    boost::endian::big_to_native_inplace(object);
    offset += sizeof(object);
}

template <Primitive T>
constexpr size_t GetObjectSerializedSize(const T& object) {
    return sizeof(object);
}

inline void SerializeObject(const std::string& object, std::vector<uint8_t>& buffer, size_t& offset) {
    const size_t size = object.size();
    SerializeObject(size, buffer, offset);
    if (!object.empty()) {
        std::memcpy(&buffer[offset], object.c_str(), object.size());
        offset += object.size();
    }
}

inline void DeserializeObject(std::string& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    DeserializeObject(size, buffer, offset);
    if (size == 0) {
        object.clear();
        return;
    }
    if (offset > buffer.size() || size > buffer.size() - offset) {
        throw std::runtime_error("DeserializeObject string out of bounds");
    }
    object.resize(size);
    std::memcpy(&object[0], &buffer[offset], size);
    offset += size;
}

inline size_t GetObjectSerializedSize(const std::string& object) {
    return sizeof(size_t) + object.size();
}

inline void SerializeObject(const uuid& object, std::vector<uint8_t>& buffer, size_t& offset) {
    const std::string uuidStr = boost::uuids::to_string(object);
    SerializeObject(uuidStr, buffer, offset);
}

inline void DeserializeObject(uuid& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    std::string uuidStr;
    DeserializeObject(uuidStr, buffer, offset);
    static boost::uuids::string_generator generator;
    object = generator(uuidStr);
}

inline size_t GetObjectSerializedSize(const uuid& object) {
    return sizeof(size_t) + 36; // Size prefix + UUID characters
}

template <Packable T>
inline void SerializeObject(const T& object, std::vector<uint8_t>& buffer, size_t& offset) {
    object.Serialize(buffer, offset);
}

template <Packable T>
inline void DeserializeObject(T& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    object.Deserialize(buffer, offset);
}

template <Packable T>
inline size_t GetObjectSerializedSize(const T& object) {
    return object.GetSerializedSize();
}

template <typename T1, typename T2>
inline void SerializeObject(const std::pair<T1, T2>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    SerializeObject(object.first, buffer, offset);
    SerializeObject(object.second, buffer, offset);
}

template <typename T1, typename T2>
inline void DeserializeObject(std::pair<T1, T2>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    DeserializeObject(object.first, buffer, offset);
    DeserializeObject(object.second, buffer, offset);
}

template <typename T1, typename T2>
inline size_t GetObjectSerializedSize(const std::pair<T1, T2>& object) {
    return GetObjectSerializedSize(object.first) + GetObjectSerializedSize(object.second);
}

template <typename T>
inline void SerializeObject(const std::optional<T>& opt, std::vector<uint8_t>& buffer, size_t& offset) {
    const bool hasValue = opt.has_value();
    SerializeObject(hasValue, buffer, offset);
    if (hasValue) {
        SerializeObject(*opt, buffer, offset);
    }
}

template <typename T>
inline void DeserializeObject(std::optional<T>& opt, const std::vector<uint8_t>& buffer, size_t& offset) {
    bool hasValue;
    DeserializeObject(hasValue, buffer, offset);
    if (hasValue) {
        T value;
        DeserializeObject(value, buffer, offset);
        opt = value;
    } else {
        opt = std::nullopt;
    }
}

template <typename T>
inline size_t GetObjectSerializedSize(const std::optional<T>& opt) {
    return sizeof(bool) + (opt.has_value() ? GetObjectSerializedSize(*opt) : 0);
}

template <typename T>
inline void SerializeObject(const std::vector<T>& object, std::vector<uint8_t>& buffer, size_t& offset) {
    const size_t size = object.size();
    SerializeObject(size, buffer, offset);
    for (const auto& element : object) {
        SerializeObject(element, buffer, offset);
    }
}

template <Primitive T>
inline void DeserializeObject(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    DeserializeObject(size, buffer, offset);
    if (offset > buffer.size() || size > (buffer.size() - offset) / sizeof(T)) {
        throw std::runtime_error("DeserializeObject vector<primitive> out of bounds");
    }
    object.resize(size);
    for (auto& element : object) {
        DeserializeObject(element, buffer, offset);
    }
}

template <typename T>
inline void DeserializeObject(std::vector<T>& object, const std::vector<uint8_t>& buffer, size_t& offset) {
    size_t size;
    DeserializeObject(size, buffer, offset);
    object.resize(size);
    for (auto& element : object) {
        DeserializeObject(element, buffer, offset);
    }
}

template <typename T>
inline size_t GetObjectSerializedSize(const std::vector<T>& object) {
    size_t result = sizeof(size_t);
    for (const auto& element : object) {
        result += GetObjectSerializedSize(element);
    }
    return result;
}

#endif //PACKABLE_H
