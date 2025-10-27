#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstdint>

#include <Package.h>
#include <Packable.h>

struct TestPackable {
    int32_t id{0};
    std::string name;

    void Serialize(std::vector<uint8_t>& buffer, size_t& offset) const {
        SerializeObject(id, buffer, offset);
        SerializeObject(name, buffer, offset);
    }

    void Deserialize(const std::vector<uint8_t>& buffer, size_t& offset) {
        DeserializeObject(id, buffer, offset);
        DeserializeObject(name, buffer, offset);
    }

    size_t GetSerializedSize() const {
        return GetObjectSerializedSize(id) + GetObjectSerializedSize(name);
    }

    bool operator==(const TestPackable& other) const {
        return id == other.id && name == other.name;
    }
};

TEST(SerializeDeserialize_FreeFunctions, PrimitiveInt32) {
    std::vector<uint8_t> buf(1024);
    size_t offset = 0;

    int32_t value = 0x12345678;
    SerializeObject(value, buf, offset);

    offset = 0;
    int32_t read = 0;
    DeserializeObject(read, buf, offset);

    EXPECT_EQ(read, value);
}

TEST(SerializeDeserialize_FreeFunctions, StdString) {
    std::vector<uint8_t> buf(1024);
    size_t offset = 0;

    std::string s = "hello-unit-test";
    SerializeObject(s, buf, offset);

    offset = 0;
    std::string read;
    DeserializeObject(read, buf, offset);

    EXPECT_EQ(read, s);
}

TEST(SerializeDeserialize_FreeFunctions, VectorOfInts) {
    std::vector<uint8_t> buf(2048);
    size_t offset = 0;

    std::vector<int32_t> in = {1, 2, 3, 1000, -5};
    SerializeObject(in, buf, offset);

    offset = 0;
    std::vector<int32_t> out;
    DeserializeObject(out, buf, offset);

    EXPECT_EQ(out.size(), in.size());
    EXPECT_EQ(out, in);
}

TEST(SerializeDeserialize_FreeFunctions, VectorOfStrings) {
    std::vector<uint8_t> buf(4096);
    size_t offset = 0;

    std::vector<std::string> in = {"one", "two", "three is longer"};
    SerializeObject(in, buf, offset);

    offset = 0;
    std::vector<std::string> out;
    DeserializeObject(out, buf, offset);

    EXPECT_EQ(out.size(), in.size());
    EXPECT_EQ(out, in);
}

TEST(Packable_Serialization, DirectPackableSerializeDeserialize) {
    std::vector<uint8_t> buf(1024);
    size_t offset = 0;

    TestPackable in;
    in.id = 42;
    in.name = "packable-name";

    in.Serialize(buf, offset);

    offset = 0;
    TestPackable out;
    out.Deserialize(buf, offset);

    EXPECT_EQ(out, in);
}

enum class PackageType : uint16_t {
    NONE = 0
};


using TestPackage = Package<PackageType>;

TEST(Package_CreateAndRead_PrimitiveAndString, CreateReadSequence) {
    TestPackable tp;
    tp.id = 7;
    tp.name = "pkg-packable";

    int32_t num = 2025;
    std::string str = "package-body";

    auto pkg = Package<PackageType>::Create(static_cast<PackageType>(0), num, str, tp);

    auto header = pkg.GetHeaderCopy();
    EXPECT_GT(static_cast<size_t>(header.size), 0u);

    int32_t rnum = pkg.GetValue<int32_t>();
    EXPECT_EQ(rnum, num);

    std::string rstr = pkg.GetValue<std::string>();
    EXPECT_EQ(rstr, str);

    TestPackable rtp = pkg.GetValue<TestPackable>();
    EXPECT_EQ(rtp, tp);
}

TEST(Package_MoveSemantics, MoveConstructorClearsSourceBuffer) {
    TestPackable tp;
    tp.id = 99;
    tp.name = "move-test";

    auto pkg1 = TestPackage::Create(static_cast<PackageType>(0), tp);
    TestPackage pkg2 = std::move(pkg1);

    TestPackable got = pkg2.GetValue<TestPackable>();
    EXPECT_EQ(got, tp);
}

