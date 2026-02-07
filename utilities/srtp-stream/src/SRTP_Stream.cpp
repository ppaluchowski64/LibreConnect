#include <SRTP_Stream.h>
#include <boost/endian.hpp>
#include <openssl/rand.h>

namespace SRTP {

    static std::atomic<bool> g_isStrpInited{false};
    static constexpr size_t MAX_PAYLOAD_SIZE = 1024;

    Stream::Stream(IOContext& context, const std::vector<uint8_t>& localKey, const std::vector<uint8_t>& remoteKey, const uint32_t framerate) : m_socket(context),
        m_context(context), m_sendSession(nullptr), m_recvSession(nullptr), m_localSSRC(0), m_remoteSSRC(0), m_timestampInc(90000 / framerate) {

        if (!g_isStrpInited.exchange(true)) {
            srtp_init();
        }

        m_sendSession = CreateSrtpSession(localKey, 3345, ssrc_any_outbound);
        m_recvSession = CreateSrtpSession(remoteKey, 3345, ssrc_any_inbound);
    }

    void Stream::Bind(const UDPEndpoint& endpoint) {
        m_socket.connect(endpoint);
    }

    void Stream::Bind(UDPEndpoint&& endpoint) {
        m_socket.connect(endpoint);
    }

    UDPEndpoint Stream::Bind() {
        m_socket.bind(UDPEndpoint(asio::ip::udp::v4(), 0));
        return m_socket.local_endpoint();
    }

    void Stream::Receive(std::vector<uint8_t>& payload) {
        std::vector<uint8_t> buffer(1500);

        int length = m_socket.receive(asio::mutable_buffer(buffer.data(), buffer.size()));

        if (srtp_unprotect(m_recvSession, buffer.data(), &length) != srtp_err_status_ok) {
            return;
        }

        if (length > sizeof(Header)) {
            payload.resize(length - sizeof(Header));
            std::memcpy(payload.data(), buffer.data() + sizeof(Header), length - sizeof(Header));
        } else {
            payload.clear();
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

            std::vector<uint8_t> outBuf(sizeof(Header) + size + 16, 0);
            std::memcpy(outBuf.data(), &header, sizeof(Header));
            std::memcpy(outBuf.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            srtp_protect(m_sendSession, outBuf.data(), &length);
            m_socket.send(asio::const_buffer(outBuf.data(), length));
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

                std::vector<uint8_t> outBuf(sizeof(Header) + 2 + fragmentSize + 16, 0);
                std::memcpy(outBuf.data(), &header, sizeof(Header));
                outBuf[sizeof(Header)] = fuIndicator;
                outBuf[sizeof(Header) + 1] = fuHeader;
                std::memcpy(outBuf.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

                int length = sizeof(Header) + 2 + fragmentSize;
                srtp_protect(m_sendSession, outBuf.data(), &length);

                m_socket.send(asio::const_buffer(outBuf.data(), length));

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

            std::vector<uint8_t> outBuf(sizeof(Header) + size + 16, 0);
            std::memcpy(outBuf.data(), &header, sizeof(Header));
            std::memcpy(outBuf.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            srtp_protect(m_sendSession, outBuf.data(), &length);
            m_socket.send(asio::const_buffer(outBuf.data(), length));
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

                std::vector<uint8_t> outBuf(sizeof(Header) + 2 + fragmentSize + 16, 0);
                std::memcpy(outBuf.data(), &header, sizeof(Header));
                outBuf[sizeof(Header)] = fuIndicator;
                outBuf[sizeof(Header) + 1] = fuHeader;
                std::memcpy(outBuf.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

                int length = sizeof(Header) + 2 + fragmentSize;
                srtp_protect(m_sendSession, outBuf.data(), &length);

                m_socket.send(asio::const_buffer(outBuf.data(), length));

                dataPtr += fragmentSize;
                remaining -= fragmentSize;
            }
        }
    }

    asio::awaitable<void> Stream::AsyncSend(const uint8_t* payload, const size_t size) {
        if (size == 0) co_return;
        const uint32_t currentTimestamp = m_timestamp.fetch_add(m_timestampInc);

        if (size <= MAX_PAYLOAD_SIZE) {
            Header header;
            BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), true);

            std::vector<uint8_t> outBuf(sizeof(Header) + size + 16, 0);
            std::memcpy(outBuf.data(), &header, sizeof(Header));
            std::memcpy(outBuf.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            srtp_protect(m_sendSession, outBuf.data(), &length);
            co_await m_socket.async_send(asio::const_buffer(outBuf.data(), length));
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

                std::vector<uint8_t> outBuf(sizeof(Header) + 2 + fragmentSize + 16, 0);
                std::memcpy(outBuf.data(), &header, sizeof(Header));
                outBuf[sizeof(Header)] = fuIndicator;
                outBuf[sizeof(Header) + 1] = fuHeader;
                std::memcpy(outBuf.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

                int length = sizeof(Header) + 2 + fragmentSize;
                srtp_protect(m_sendSession, outBuf.data(), &length);

                co_await m_socket.async_send(asio::const_buffer(outBuf.data(), length));

                dataPtr += fragmentSize;
                remaining -= fragmentSize;
            }
        }
    }

    asio::awaitable<void> Stream::AsyncReceive(std::vector<uint8_t>& payload) {
        std::vector<uint8_t> buffer(1500);

        int length = co_await m_socket.async_receive(
            asio::mutable_buffer(buffer.data(), buffer.size()),
            asio::use_awaitable
        );

        if (srtp_unprotect(m_recvSession, buffer.data(), &length) != srtp_err_status_ok) {
            co_return;
        }

        if (length > sizeof(Header)) {
            payload.resize(length - sizeof(Header));
            std::memcpy(payload.data(), buffer.data() + sizeof(Header), length - sizeof(Header));
        } else {
            payload.clear();
        }
    }

    asio::awaitable<void> Stream::AsyncSend(const std::vector<uint8_t>& payloadData) {
        const uint64_t size = payloadData.size();
        const uint8_t* payload = payloadData.data();

        if (size == 0) co_return;
        const uint32_t currentTimestamp = m_timestamp.fetch_add(m_timestampInc);

        if (size <= MAX_PAYLOAD_SIZE) {
            Header header;
            BuildRtpHeader(&header, currentTimestamp, m_sequence.fetch_add(1), true);

            std::vector<uint8_t> outBuf(sizeof(Header) + size + 16, 0);
            std::memcpy(outBuf.data(), &header, sizeof(Header));
            std::memcpy(outBuf.data() + sizeof(Header), payload, size);

            int length = sizeof(Header) + size;
            srtp_protect(m_sendSession, outBuf.data(), &length);
            co_await m_socket.async_send(asio::const_buffer(outBuf.data(), length));
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

                std::vector<uint8_t> outBuf(sizeof(Header) + 2 + fragmentSize + 16, 0);
                std::memcpy(outBuf.data(), &header, sizeof(Header));
                outBuf[sizeof(Header)] = fuIndicator;
                outBuf[sizeof(Header) + 1] = fuHeader;
                std::memcpy(outBuf.data() + sizeof(Header) + 2, dataPtr, fragmentSize);

                int length = sizeof(Header) + 2 + fragmentSize;
                srtp_protect(m_sendSession, outBuf.data(), &length);

                co_await m_socket.async_send(asio::const_buffer(outBuf.data(), length));

                dataPtr += fragmentSize;
                remaining -= fragmentSize;
            }
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
