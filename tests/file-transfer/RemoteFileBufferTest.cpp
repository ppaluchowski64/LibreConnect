#include <RemoteFileBuffer.h>
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <thread>

TEST(RemoteFileBuffer, WrapsAndAppliesBackpressure) {
    RemoteFileBuffer buffer(5);
    const std::array<uint8_t, 4> first{1, 2, 3, 4};
    const std::array<uint8_t, 3> second{5, 6, 7};
    ASSERT_TRUE(buffer.TryWrite(first));
    ASSERT_FALSE(buffer.TryWrite(second));
    std::array<uint8_t, 2> prefix{};
    EXPECT_EQ(buffer.Read(prefix).bytes, 2);
    EXPECT_EQ(prefix, (std::array<uint8_t, 2>{1, 2}));
    ASSERT_TRUE(buffer.TryWrite(second));
    buffer.Finish(true);
    std::array<uint8_t, 8> output{};
    const RemoteFileBuffer::ReadResult result = buffer.Read(output);
    EXPECT_EQ(result.bytes, 5);
    EXPECT_TRUE(result.finished);
    EXPECT_FALSE(result.failed);
    EXPECT_EQ((std::vector<uint8_t>(output.begin(), output.begin() + 5)), (std::vector<uint8_t>{3, 4, 5, 6, 7}));
}

TEST(RemoteFileBuffer, CancellationDropsPendingBytesAndClosesProducerOnce) {
    RemoteFileBuffer buffer;
    int calls = 0;
    buffer.SetCancelAction([&calls]() { ++calls; });
    ASSERT_TRUE(buffer.TryWrite(std::array<uint8_t, 3>{1, 2, 3}));
    buffer.Cancel();
    buffer.Cancel();
    buffer.Finish(true);
    EXPECT_EQ(calls, 1);
    std::array<uint8_t, 4> bytes{};
    const RemoteFileBuffer::ReadResult result = buffer.Read(bytes);
    EXPECT_EQ(result.bytes, 0);
    EXPECT_TRUE(result.failed);
    EXPECT_TRUE(result.finished);
    EXPECT_FALSE(buffer.TryWrite(bytes));
}

TEST(RemoteFileBuffer, CancellationBeforeChannelAssignmentClosesLateChannel) {
    RemoteFileBuffer buffer;
    buffer.Cancel();
    int calls = 0;
    buffer.SetCancelAction([&calls]() { ++calls; });
    EXPECT_EQ(calls, 1);
}

TEST(RemoteFileBuffer, CancellationDoesNotThrowIntoCom) {
    RemoteFileBuffer buffer;
    buffer.SetCancelAction([]() { throw std::runtime_error("executor stopped"); });
    EXPECT_NO_THROW(buffer.Cancel());
    EXPECT_TRUE(buffer.Cancelled());
}

TEST(RemoteFileBuffer, ConcurrentProducerAndConsumerPreserveFileContents) {
    RemoteFileBuffer buffer(1024);
    constexpr size_t length = 2 * 1024 * 1024;
    std::thread producer([&buffer]() {
        std::array<uint8_t, 257> bytes{};
        for (size_t offset = 0; offset < length;) {
            const size_t count = std::min(bytes.size(), length - offset);
            for (size_t i = 0; i < count; ++i)
                bytes[i] = static_cast<uint8_t>((offset + i) % 251);
            while (!buffer.TryWrite(std::span<const uint8_t>(bytes.data(), count)))
                std::this_thread::yield();
            offset += count;
        }
        buffer.Finish(true);
    });
    size_t received = 0;
    bool matches = true;
    for (;;) {
        std::array<uint8_t, 389> bytes{};
        const RemoteFileBuffer::ReadResult result = buffer.Read(bytes);
        for (size_t i = 0; i < result.bytes; ++i)
            matches = matches && bytes[i] == static_cast<uint8_t>((received + i) % 251);
        received += result.bytes;
        if (result.finished)
            break;
        std::this_thread::yield();
    }
    producer.join();
    EXPECT_EQ(received, length);
    EXPECT_TRUE(matches);
}

TEST(RemoteFileBuffer, InterruptedProducerUnblocksFullBuffer) {
    RemoteFileBuffer buffer(16);
    std::atomic<bool> started{false};
    std::thread producer([&buffer, &started]() {
        const std::array<uint8_t, 16> bytes{};
        buffer.TryWrite(bytes);
        started.store(true);
        while (!buffer.TryWrite(bytes) && !buffer.Cancelled())
            std::this_thread::yield();
        buffer.Finish(false);
    });
    while (!started.load())
        std::this_thread::yield();
    buffer.Cancel();
    producer.join();
    std::array<uint8_t, 16> bytes{};
    EXPECT_TRUE(buffer.Read(bytes).failed);
}
