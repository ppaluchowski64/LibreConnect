#include <SRTP_Stream.h>
#include <DebugLog.h>
#include <boost/endian.hpp>
#include <asio/awaitable.hpp>
#include <asio.hpp>
#include <openssl/rand.h>
#include <array>
#include <algorithm>
#include <unordered_map>
#include <asio/experimental/awaitable_operators.hpp>

namespace SRTP {

    static std::atomic<bool> g_isStrpInited{false};
    static constexpr size_t MAX_PAYLOAD_SIZE = 1350;
    static constexpr size_t SOCKET_BUFFER_SIZE = 4 * 1024 * 1024;
    static std::atomic<uint32_t> g_unprotectFailCount{0};
    static std::atomic<uint32_t> g_protectFailCount{0};
    static std::atomic<uint64_t> g_rtpReceivedCount{0};

    static asio::awaitable<bool> AsyncSendNoThrow(UDPSocket& socket, const void* data, const size_t size) {
        std::error_code ec;
        co_await socket.async_send(
            asio::const_buffer(data, size),
            asio::redirect_error(asio::use_awaitable, ec)
        );
        co_return !ec;
    }

    static bool SendNoThrow(UDPSocket& socket, const void* data, const size_t size) {
        std::error_code ec;
        socket.send(asio::const_buffer(data, size), 0, ec);
        return !ec;
    }

    Stream::Stream(IOContext& context, const std::vector<uint8_t>& localKey, const std::vector<uint8_t>& remoteKey, uint32_t framerate) : m_socket(context), m_context(context),
        m_sendSession(nullptr), m_recvSession(nullptr), m_localSSRC(0), m_remoteSSRC(0), m_stopReceivingSignal(m_context.get_executor()) {

        if (framerate == 0) {
            framerate = 30;
        }

        m_timestampInc = 90000 / framerate;

        if (!g_isStrpInited.exchange(true)) {
            if (srtp_init() != srtp_err_status_ok) {
                Debug::LogError("SRTP init failed");
            }
        }

        m_sendSession = CreateSrtpSession(localKey, 3345, ssrc_any_outbound);
        m_recvSession = CreateSrtpSession(remoteKey, 3345, ssrc_any_inbound);
        m_buffer.resize(1500);
    }

    void Stream::Bind(const UDPEndpoint& endpoint) {
        if (!m_socket.is_open()) {
            m_socket.open(endpoint.protocol());
        }
        ConfigureSocketBuffers();
        m_socket.connect(endpoint);
        Debug::Log("SRTP bind: {}:{}", endpoint.address().to_string(), endpoint.port());
    }

    void Stream::Bind(UDPEndpoint&& endpoint) {
        if (!m_socket.is_open()) {
            m_socket.open(endpoint.protocol());
        }
        ConfigureSocketBuffers();
        m_socket.connect(endpoint);
        Debug::Log("SRTP bind: {}:{}", endpoint.address().to_string(), endpoint.port());
    }

    UDPEndpoint Stream::Bind() {
        if (!m_socket.is_open()) {
            m_socket.open(asio::ip::udp::v4());
        }
        ConfigureSocketBuffers();
        m_socket.bind(UDPEndpoint(asio::ip::udp::v4(), 0));
        return m_socket.local_endpoint();
    }

    void Stream::Close() {
        std::error_code ec;
        m_socket.cancel(ec);
        m_socket.close(ec);
    }

    void Stream::Receive(std::vector<uint8_t>& payload) {
        payload.clear();
        bool frameActive = false;
        bool markerSeen = false;
        uint16_t expectedSeq = 0;
        uint16_t markerSeq = 0;
        uint32_t frameTimestamp = 0;
        static constexpr uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
        std::unordered_map<uint16_t, std::vector<uint8_t>> pendingPayloads;
        static constexpr size_t MAX_REORDERED_PACKETS = 2048;

        while (true) {
            int length = m_socket.receive(asio::mutable_buffer(m_buffer.data(), m_buffer.size()));

            if (srtp_unprotect(m_recvSession, m_buffer.data(), &length) != srtp_err_status_ok) {
                m_receiveLossSignal.fetch_add(1);
                if ((++g_unprotectFailCount % 100) == 1) {
                    Debug::LogWarning("SRTP unprotect failed (count={})", g_unprotectFailCount.load());
                }
                continue;
            }

            if (length <= sizeof(Header)) continue;

            const Header* header = reinterpret_cast<Header*>(m_buffer.data());
            const bool frameComplete = (header->m_pt & 0x80) != 0;
            const uint16_t seq = boost::endian::big_to_native(header->seq);
            const uint32_t timestamp = boost::endian::big_to_native(header->timestamp);
            g_rtpReceivedCount.fetch_add(1);

            const uint8_t* rtpPayload = m_buffer.data() + sizeof(Header);
            const size_t rtpPayloadLen = length - sizeof(Header);
            if (rtpPayloadLen == 0) continue;

            if (!frameActive || timestamp != frameTimestamp) {
                if (frameActive) {
                    m_receiveLossSignal.fetch_add(1);
                }
                frameActive = true;
                markerSeen = false;
                payload.clear();
                pendingPayloads.clear();
                frameTimestamp = timestamp;
                expectedSeq = seq;
            }

            const uint8_t firstByte = rtpPayload[0];
            const uint8_t nalType = firstByte & 0x1F;
            std::vector<uint8_t> chunk;

            if (nalType == 28) {
                if (rtpPayloadLen < 2) {
                    m_receiveLossSignal.fetch_add(1);
                    payload.clear();
                    pendingPayloads.clear();
                    frameActive = false;
                    markerSeen = false;
                    continue;
                }

                const uint8_t fuHeader = rtpPayload[1];
                const bool isStart = (fuHeader & 0x80) != 0;

                if (isStart) {
                    chunk.insert(chunk.end(), kStartCode, kStartCode + sizeof(kStartCode));
                    uint8_t reconstructedNalHeader = (firstByte & 0xE0) | (fuHeader & 0x1F);
                    chunk.push_back(reconstructedNalHeader);
                }

                chunk.insert(chunk.end(), rtpPayload + 2, rtpPayload + rtpPayloadLen);
            } else {
                chunk.insert(chunk.end(), kStartCode, kStartCode + sizeof(kStartCode));
                chunk.insert(chunk.end(), rtpPayload, rtpPayload + rtpPayloadLen);
            }

            pendingPayloads.try_emplace(seq, std::move(chunk));
            if (frameComplete) {
                markerSeen = true;
                markerSeq = seq;
            }

            if (pendingPayloads.size() > MAX_REORDERED_PACKETS) {
                m_receiveLossSignal.fetch_add(1);
                payload.clear();
                pendingPayloads.clear();
                frameActive = false;
                markerSeen = false;
                continue;
            }

            bool dropFrame = false;
            while (true) {
                auto it = pendingPayloads.find(expectedSeq);
                if (it == pendingPayloads.end()) break;

                const std::vector<uint8_t>& orderedChunk = it->second;
                if (payload.empty()) {
                    if (orderedChunk.size() < sizeof(kStartCode) ||
                        !std::equal(kStartCode, kStartCode + sizeof(kStartCode), orderedChunk.begin())) {
                        m_receiveLossSignal.fetch_add(1);
                        payload.clear();
                        pendingPayloads.clear();
                        frameActive = false;
                        markerSeen = false;
                        dropFrame = true;
                        break;
                    }
                }

                payload.insert(payload.end(), orderedChunk.begin(), orderedChunk.end());
                pendingPayloads.erase(it);

                if (markerSeen && expectedSeq == markerSeq) {
                    return;
                }
                expectedSeq = static_cast<uint16_t>(expectedSeq + 1);
            }

            if (dropFrame) {
                continue;
            }
        }
    }

    void Stream::Send(const std::vector<uint8_t>& payloadData) {
        const uint64_t size = payloadData.size();
        const uint8_t* payload = payloadData.data();

        if (size == 0) return;
        const uint32_t currentTimestamp = m_timestamp.fetch_add(m_timestampInc);

        if (size <= MAX_PAYLOAD_SIZE) {
            Header header;
            BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), true);

            m_buffer.resize(sizeof(Header) + size + 16, 0);
            std::memcpy(m_buffer.data(), &header, sizeof(Header));
            std::memcpy(m_buffer.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            if (srtp_protect(m_sendSession, m_buffer.data(), &length) != srtp_err_status_ok) {
                if ((++g_protectFailCount % 100) == 1) {
                    Debug::LogWarning("SRTP protect failed (count={})", g_protectFailCount.load());
                }
                return;
            }
            m_socket.send(asio::const_buffer(m_buffer.data(), length));
        } else {
            const uint8_t nalHeader = payload[0];
            const uint8_t fuIndicator = (nalHeader & 0xE0) | 28;
            const uint8_t nalType = nalHeader & 0x1F;

            const uint8_t* dataPtr = payload + 1;
            size_t remaining = size - 1;

            while (remaining > 0) {
                size_t fragmentSize = std::min(remaining, MAX_PAYLOAD_SIZE);
                const bool isFirst = (dataPtr == payload + 1);
                const bool isLast = (remaining <= MAX_PAYLOAD_SIZE);

                uint8_t fuHeader = nalType;
                if (isFirst) fuHeader |= 0x80;
                if (isLast)  fuHeader |= 0x40;

                Header header;
                BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), isLast);

                m_buffer.resize(sizeof(Header) + 2 + fragmentSize + 16, 0);
                std::memcpy(m_buffer.data(), &header, sizeof(Header));
                m_buffer[sizeof(Header)] = fuIndicator;
                m_buffer[sizeof(Header) + 1] = fuHeader;
                std::memcpy(m_buffer.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

                int length = sizeof(Header) + 2 + fragmentSize;
                if (srtp_protect(m_sendSession, m_buffer.data(), &length) != srtp_err_status_ok) {
                    if ((++g_protectFailCount % 100) == 1) {
                        Debug::LogWarning("SRTP protect failed (count={})", g_protectFailCount.load());
                    }
                    return;
                }

                m_socket.send(asio::const_buffer(m_buffer.data(), length));

                dataPtr += fragmentSize;
                remaining -= fragmentSize;
            }
        }
    }

    void Stream::Send(const uint8_t* payload, const size_t size) {
        if (size == 0) return;
        const uint32_t currentTimestamp = m_timestamp.fetch_add(m_timestampInc);

        if (size <= MAX_PAYLOAD_SIZE) {
            Header header;
            BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), true);

            m_buffer.resize(sizeof(Header) + size + 16, 0);
            std::memcpy(m_buffer.data(), &header, sizeof(Header));
            std::memcpy(m_buffer.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            srtp_protect(m_sendSession, m_buffer.data(), &length);
            m_socket.send(asio::const_buffer(m_buffer.data(), length));
        } else {
            const uint8_t nalHeader = payload[0];
            const uint8_t fuIndicator = (nalHeader & 0xE0) | 28;
            const uint8_t nalType = nalHeader & 0x1F;

            const uint8_t* dataPtr = payload + 1;
            size_t remaining = size - 1;

            while (remaining > 0) {
                size_t fragmentSize = std::min(remaining, MAX_PAYLOAD_SIZE);
                const bool isFirst = (dataPtr == payload + 1);
                const bool isLast = (remaining <= MAX_PAYLOAD_SIZE);

                uint8_t fuHeader = nalType;
                if (isFirst) fuHeader |= 0x80;
                if (isLast)  fuHeader |= 0x40;

                Header header;
                BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), isLast);

                m_buffer.resize(sizeof(Header) + 2 + fragmentSize + 16, 0);
                std::memcpy(m_buffer.data(), &header, sizeof(Header));
                m_buffer[sizeof(Header)] = fuIndicator;
                m_buffer[sizeof(Header) + 1] = fuHeader;
                std::memcpy(m_buffer.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

                int length = sizeof(Header) + 2 + fragmentSize;
                srtp_protect(m_sendSession, m_buffer.data(), &length);

                m_socket.send(asio::const_buffer(m_buffer.data(), length));

                dataPtr += fragmentSize;
                remaining -= fragmentSize;
            }
        }
    }

    asio::awaitable<void> Stream::AsyncSend(const uint8_t* payload, const size_t size) {
        if (size == 0) co_return;
        const uint32_t currentTimestamp = m_timestamp.fetch_add(m_timestampInc);
        std::array<uint8_t, sizeof(Header) + 2 + MAX_PAYLOAD_SIZE + 16> sendBuffer{};

        if (size <= MAX_PAYLOAD_SIZE) {
            Header header;
            BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), true);

            std::memcpy(sendBuffer.data(), &header, sizeof(Header));
            std::memcpy(sendBuffer.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            if (srtp_protect(m_sendSession, sendBuffer.data(), &length) != srtp_err_status_ok) {
                if ((++g_protectFailCount % 100) == 1) {
                    Debug::LogWarning("SRTP protect failed (count={})", g_protectFailCount.load());
                }
                co_return;
            }

            if (!SendNoThrow(m_socket, sendBuffer.data(), static_cast<size_t>(length))) {
                co_return;
            }
        } else {
            const uint8_t nalHeader = payload[0];
            const uint8_t fuIndicator = (nalHeader & 0xE0) | 28;
            const uint8_t nalType = nalHeader & 0x1F;

            const uint8_t* dataPtr = payload + 1;
            size_t remaining = size - 1;

            while (remaining > 0) {
                size_t fragmentSize = std::min(remaining, MAX_PAYLOAD_SIZE);
                const bool isFirst = (dataPtr == payload + 1);
                const bool isLast = (remaining <= MAX_PAYLOAD_SIZE);

                uint8_t fuHeader = nalType;
                if (isFirst) fuHeader |= 0x80;
                if (isLast)  fuHeader |= 0x40;

                Header header;
                BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), isLast);

                std::memcpy(sendBuffer.data(), &header, sizeof(Header));
                sendBuffer[sizeof(Header)] = fuIndicator;
                sendBuffer[sizeof(Header) + 1] = fuHeader;
                std::memcpy(sendBuffer.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

                int length = sizeof(Header) + 2 + fragmentSize;
                if (srtp_protect(m_sendSession, sendBuffer.data(), &length) != srtp_err_status_ok) {
                    if ((++g_protectFailCount % 100) == 1) {
                        Debug::LogWarning("SRTP protect failed (count={})", g_protectFailCount.load());
                    }
                    co_return;
                }

                if (!SendNoThrow(m_socket, sendBuffer.data(), static_cast<size_t>(length))) {
                    co_return;
                }

                dataPtr += fragmentSize;
                remaining -= fragmentSize;
            }
        }
    }

    asio::awaitable<void> Stream::AsyncSendNal(const uint8_t* payload, const size_t size, const uint32_t timestamp, const bool marker) {
        if (size == 0) co_return;
        std::array<uint8_t, sizeof(Header) + 2 + MAX_PAYLOAD_SIZE + 16> sendBuffer{};

        if (size <= MAX_PAYLOAD_SIZE) {
            Header header;
            BuildRtpHeader(&header, timestamp, m_sequence.fetch_add(1), marker);

            std::memcpy(sendBuffer.data(), &header, sizeof(Header));
            std::memcpy(sendBuffer.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            if (srtp_protect(m_sendSession, sendBuffer.data(), &length) != srtp_err_status_ok) {
                if ((++g_protectFailCount % 100) == 1) {
                    Debug::LogWarning("SRTP protect failed (count={})", g_protectFailCount.load());
                }
                co_return;
            }

            if (!SendNoThrow(m_socket, sendBuffer.data(), static_cast<size_t>(length))) {
                co_return;
            }
            co_return;
        }

        const uint8_t nalHeader = payload[0];
        const uint8_t fuIndicator = (nalHeader & 0xE0) | 28;
        const uint8_t nalType = nalHeader & 0x1F;

        const uint8_t* dataPtr = payload + 1;
        size_t remaining = size - 1;

        while (remaining > 0) {
            size_t fragmentSize = std::min(remaining, MAX_PAYLOAD_SIZE);
            const bool isFirst = (dataPtr == payload + 1);
            const bool isLast = (remaining <= MAX_PAYLOAD_SIZE);

            uint8_t fuHeader = nalType;
            if (isFirst) fuHeader |= 0x80;
            if (isLast)  fuHeader |= 0x40;

            const bool markerBit = marker && isLast;

            Header header;
            BuildRtpHeader(&header, timestamp, m_sequence.fetch_add(1), markerBit);

            std::memcpy(sendBuffer.data(), &header, sizeof(Header));
            sendBuffer[sizeof(Header)] = fuIndicator;
            sendBuffer[sizeof(Header) + 1] = fuHeader;
            std::memcpy(sendBuffer.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

            int length = sizeof(Header) + 2 + fragmentSize;
            if (srtp_protect(m_sendSession, sendBuffer.data(), &length) != srtp_err_status_ok) {
                if ((++g_protectFailCount % 100) == 1) {
                    Debug::LogWarning("SRTP protect failed (count={})", g_protectFailCount.load());
                }
                co_return;
            }

            if (!SendNoThrow(m_socket, sendBuffer.data(), static_cast<size_t>(length))) {
                co_return;
            }

            dataPtr += fragmentSize;
            remaining -= fragmentSize;
        }
    }

    uint32_t Stream::NextTimestamp() {
        return m_timestamp.fetch_add(m_timestampInc);
    }

    bool Stream::ConsumeReceiveLossSignal() {
        return m_receiveLossSignal.exchange(0) != 0;
    }

    asio::awaitable<void> Stream::AsyncReceive(std::vector<uint8_t>& payload) {
        if (m_isReceiving.exchange(true)) {
            m_stopReceiving.store(true);
            m_stopReceivingSignal.cancel();
            co_return;
        }

        struct ReceiveGuard {
            std::atomic<bool>& isReceiving;
            std::atomic<bool>& stopReceiving;
            ~ReceiveGuard() {
                stopReceiving.store(false);
                isReceiving.store(false);
            }
        } guard{m_isReceiving, m_stopReceiving};

        m_stopReceiving.store(false);
        m_stopReceivingSignal.expires_at((asio::steady_timer::time_point::max)());

        payload.clear();

        bool frameActive = false;
        bool markerSeen = false;
        uint16_t expectedSeq = 0;
        uint16_t markerSeq = 0;
        uint32_t frameTimestamp = 0;

        static constexpr uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
        std::unordered_map<uint16_t, std::vector<uint8_t>> pendingPayloads;
        static constexpr size_t MAX_REORDERED_PACKETS = 2048;

        while (!m_stopReceiving.load()) {
            std::error_code ec;
            std::error_code stopEc;
            (void)stopEc;
            int length{};

            {
                using namespace asio::experimental::awaitable_operators;
                auto result = co_await (
                    m_socket.async_receive(
                        asio::buffer(m_buffer),
                        asio::redirect_error(asio::use_awaitable, ec)
                    )
                    || m_stopReceivingSignal.async_wait(asio::redirect_error(asio::use_awaitable, stopEc))
                );

                if (result.index() == 0) {
                    length = std::get<0>(result);
                } else {
                    payload.clear();
                    break;
                }
            }

            if (ec) {
                payload.clear();
                break;
            }

            if (srtp_unprotect(m_recvSession, m_buffer.data(), &length) != srtp_err_status_ok) {
                m_receiveLossSignal.fetch_add(1);
                if ((++g_unprotectFailCount % 100) == 1) {
                    Debug::LogWarning("SRTP unprotect failed (count={})", g_unprotectFailCount.load());
                }
                continue;
            }

            if (length <= sizeof(Header)) continue;

            const Header* header = reinterpret_cast<Header*>(m_buffer.data());
            const bool frameComplete = (header->m_pt & 0x80) != 0;
            const uint16_t seq = boost::endian::big_to_native(header->seq);
            const uint32_t timestamp = boost::endian::big_to_native(header->timestamp);
            g_rtpReceivedCount.fetch_add(1);

            const uint8_t* rtpPayload = m_buffer.data() + sizeof(Header);
            const size_t rtpPayloadLen = length - sizeof(Header);
            if (rtpPayloadLen == 0) continue;

            if (!frameActive || timestamp != frameTimestamp) {
                if (frameActive) {
                    m_receiveLossSignal.fetch_add(1);
                }
                frameActive = true;
                markerSeen = false;
                payload.clear();
                pendingPayloads.clear();
                frameTimestamp = timestamp;
                expectedSeq = seq;
            }

            const uint8_t firstByte = rtpPayload[0];
            const uint8_t nalType = firstByte & 0x1F;
            std::vector<uint8_t> chunk;

            if (nalType == 28) {
                if (rtpPayloadLen < 2) {
                    m_receiveLossSignal.fetch_add(1);
                    payload.clear();
                    pendingPayloads.clear();
                    frameActive = false;
                    continue;
                }

                const uint8_t fuHeader = rtpPayload[1];
                const bool isStart = (fuHeader & 0x80) != 0;

                if (isStart) {
                    chunk.insert(chunk.end(), kStartCode, kStartCode + sizeof(kStartCode));
                    uint8_t reconstructedNalHeader = (firstByte & 0xE0) | (fuHeader & 0x1F);
                    chunk.push_back(reconstructedNalHeader);
                }

                chunk.insert(chunk.end(), rtpPayload + 2, rtpPayload + rtpPayloadLen);
            } else {
                chunk.insert(chunk.end(), kStartCode, kStartCode + sizeof(kStartCode));
                chunk.insert(chunk.end(), rtpPayload, rtpPayload + rtpPayloadLen);
            }

            pendingPayloads.try_emplace(seq, std::move(chunk));
            if (frameComplete) {
                markerSeen = true;
                markerSeq = seq;
            }

            if (pendingPayloads.size() > MAX_REORDERED_PACKETS) {
                m_receiveLossSignal.fetch_add(1);
                payload.clear();
                pendingPayloads.clear();
                frameActive = false;
                markerSeen = false;
                continue;
            }

            bool dropFrame = false;
            while (true) {
                auto it = pendingPayloads.find(expectedSeq);
                if (it == pendingPayloads.end()) break;

                const std::vector<uint8_t>& orderedChunk = it->second;
                if (payload.empty()) {
                    if (orderedChunk.size() < sizeof(kStartCode) ||
                        !std::equal(kStartCode, kStartCode + sizeof(kStartCode), orderedChunk.begin())) {
                        m_receiveLossSignal.fetch_add(1);
                        payload.clear();
                        pendingPayloads.clear();
                        frameActive = false;
                        markerSeen = false;
                        dropFrame = true;
                        break;
                    }
                }

                payload.insert(payload.end(), orderedChunk.begin(), orderedChunk.end());
                pendingPayloads.erase(it);

                if (markerSeen && expectedSeq == markerSeq) {
                    co_return;
                }
                expectedSeq = static_cast<uint16_t>(expectedSeq + 1);
            }

            if (dropFrame) {
                continue;
            }
        }
    }

    // asio::awaitable<void> Stream::AsyncReceive(std::vector<uint8_t>& payload) {
    //     payload.clear();
    //     bool assemblingFrame = false;
    //     bool dropCurrentFrame = false;
    //     uint16_t expectedSeq = 0;
    //     uint32_t currentFrameTimestamp = 0;
    //     static constexpr uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};
    //
    //     while (true) {
    //         std::error_code ec;
    //         int length = static_cast<int>(co_await m_socket.async_receive(
    //             asio::mutable_buffer(m_buffer.data(), m_buffer.size()),
    //             asio::redirect_error(asio::use_awaitable, ec)
    //         ));
    //         if (ec) {
    //             co_return;
    //         }
    //
    //         if (srtp_unprotect(m_recvSession, m_buffer.data(), &length) != srtp_err_status_ok) {
    //             m_receiveLossSignal.fetch_add(1);
    //             if ((++g_unprotectFailCount % 100) == 1) {
    //                 Debug::LogWarning("SRTP unprotect failed (count={})", g_unprotectFailCount.load());
    //             }
    //             continue;
    //         }
    //
    //         if (length <= sizeof(Header)) continue;
    //
    //         const Header* header = reinterpret_cast<Header*>(m_buffer.data());
    //         const bool frameComplete = (header->m_pt & 0x80) != 0;
    //         const uint16_t seq = boost::endian::big_to_native(header->seq);
    //         const uint32_t timestamp = boost::endian::big_to_native(header->timestamp);
    //         g_rtpReceivedCount.fetch_add(1);
    //
    //         const uint8_t* rtpPayload = m_buffer.data() + sizeof(Header);
    //         const size_t rtpPayloadLen = length - sizeof(Header);
    //         if (rtpPayloadLen == 0) continue;
    //
    //         if (!assemblingFrame) {
    //             assemblingFrame = true;
    //             dropCurrentFrame = false;
    //             payload.clear();
    //             currentFrameTimestamp = timestamp;
    //             expectedSeq = static_cast<uint16_t>(seq + 1);
    //         } else {
    //             if (seq != expectedSeq || timestamp != currentFrameTimestamp) {
    //                 const uint16_t lost = TrackRtpSequenceGap(expectedSeq, seq);
    //                 if (lost > 0 || timestamp != currentFrameTimestamp) {
    //                     m_receiveLossSignal.fetch_add(1);
    //                 }
    //                 dropCurrentFrame = true;
    //             }
    //             expectedSeq = static_cast<uint16_t>(seq + 1);
    //         }
    //
    //         if (!dropCurrentFrame) {
    //             const uint8_t firstByte = rtpPayload[0];
    //             const uint8_t nalType = firstByte & 0x1F;
    //
    //             if (nalType == 28) {
    //                 if (rtpPayloadLen < 2) {
    //                     dropCurrentFrame = true;
    //                 } else {
    //                     const uint8_t fuHeader = rtpPayload[1];
    //                     const bool isStart = (fuHeader & 0x80) != 0;
    //
    //                     if (payload.empty() && !isStart) {
    //                         dropCurrentFrame = true;
    //                     } else {
    //                         if (isStart) {
    //                             payload.insert(payload.end(), kStartCode, kStartCode + sizeof(kStartCode));
    //                             uint8_t reconstructedNalHeader = (firstByte & 0xE0) | (fuHeader & 0x1F);
    //                             payload.push_back(reconstructedNalHeader);
    //                         }
    //
    //                         payload.insert(payload.end(), rtpPayload + 2, rtpPayload + rtpPayloadLen);
    //                     }
    //                 }
    //             } else {
    //                 payload.insert(payload.end(), kStartCode, kStartCode + sizeof(kStartCode));
    //                 payload.insert(payload.end(), rtpPayload, rtpPayload + rtpPayloadLen);
    //             }
    //         }
    //
    //         if (frameComplete) {
    //             if (!dropCurrentFrame && !payload.empty()) {
    //                 co_return;
    //             }
    //
    //             payload.clear();
    //             assemblingFrame = false;
    //             dropCurrentFrame = false;
    //         }
    //     }
    // }

    asio::awaitable<void> Stream::AsyncSend(const std::vector<uint8_t>& payloadData) {
        const uint64_t size = payloadData.size();
        const uint8_t* payload = payloadData.data();

        if (size == 0) co_return;
        const uint32_t currentTimestamp = m_timestamp.fetch_add(m_timestampInc);
        std::array<uint8_t, sizeof(Header) + 2 + MAX_PAYLOAD_SIZE + 16> sendBuffer{};

        if (size <= MAX_PAYLOAD_SIZE) {
            Header header;
            BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), true);

            std::memcpy(sendBuffer.data(), &header, sizeof(Header));
            std::memcpy(sendBuffer.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            srtp_protect(m_sendSession, sendBuffer.data(), &length);
            if (!SendNoThrow(m_socket, sendBuffer.data(), static_cast<size_t>(length))) {
                co_return;
            }
        } else {
            const uint8_t nalHeader = payload[0];
            const uint8_t fuIndicator = (nalHeader & 0xE0) | 28;
            const uint8_t nalType = nalHeader & 0x1F;

            const uint8_t* dataPtr = payload + 1;
            size_t remaining = size - 1;

            while (remaining > 0) {
                size_t fragmentSize = std::min(remaining, MAX_PAYLOAD_SIZE);
                const bool isFirst = (dataPtr == payload + 1);
                const bool isLast = (remaining <= MAX_PAYLOAD_SIZE);

                uint8_t fuHeader = nalType;
                if (isFirst) fuHeader |= 0x80;
                if (isLast)  fuHeader |= 0x40;

                Header header;
                BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), isLast);

                std::memcpy(sendBuffer.data(), &header, sizeof(Header));
                sendBuffer[sizeof(Header)] = fuIndicator;
                sendBuffer[sizeof(Header) + 1] = fuHeader;
                std::memcpy(sendBuffer.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

                int length = sizeof(Header) + 2 + fragmentSize;
                srtp_protect(m_sendSession, sendBuffer.data(), &length);

                if (!SendNoThrow(m_socket, sendBuffer.data(), static_cast<size_t>(length))) {
                    co_return;
                }

                dataPtr += fragmentSize;
                remaining -= fragmentSize;
            }
        }
    }

    void Stream::ConfigureSocketBuffers() {
        std::error_code ec;
        m_socket.set_option(asio::socket_base::send_buffer_size(static_cast<int>(SOCKET_BUFFER_SIZE)), ec);
        if (ec) {
            Debug::LogWarning("Failed to set UDP send buffer size to {} bytes: {}", SOCKET_BUFFER_SIZE, ec.message());
        }

        ec.clear();
        m_socket.set_option(asio::socket_base::receive_buffer_size(static_cast<int>(SOCKET_BUFFER_SIZE)), ec);
        if (ec) {
            Debug::LogWarning("Failed to set UDP receive buffer size to {} bytes: {}", SOCKET_BUFFER_SIZE, ec.message());
        }
    }

    std::vector<uint8_t> Stream::GenerateKey() {
        constexpr size_t KEY_SIZE = 32;

        std::vector<uint8_t> key(KEY_SIZE);

        if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) {
            Debug::LogError("OpenSSL RAND_bytes failed");
            throw;
        }

        return key;
    }

    srtp_t Stream::CreateSrtpSession(const std::vector<uint8_t>& key, const uint32_t ssrc, const srtp_ssrc_type_t type) {
        srtp_policy_t policy = {};

        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
        srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

        policy.ssrc.type = type;
        policy.ssrc.value = ssrc;
        policy.key = const_cast<uint8_t*>(key.data());
        policy.next = nullptr;

        srtp_t session;
        if (srtp_create(&session, &policy) != srtp_err_status_ok) {
            Debug::LogError("Failed to create SRTP session");
        }

        return session;
    }


    void Stream::BuildRtpHeader(void* dst, const uint32_t timestamp, const uint16_t sequence, const bool marker) const {
        constexpr uint8_t PayloadType = 96;

        Header header{};
        header.v_p_x_cc = 0x80;

        header.m_pt = (marker ? 0x80 : 0x00) | (PayloadType & 0x7F);
        header.seq = boost::endian::native_to_big(sequence);
        header.timestamp = boost::endian::native_to_big(timestamp);
        header.ssrc = boost::endian::native_to_big(m_localSSRC);

        std::memcpy(dst, &header, sizeof(header));
    }
}
