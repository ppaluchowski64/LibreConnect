#include <SRTP_Stream.h>
#include <boost/endian.hpp>
#include <openssl/rand.h>

namespace SRTP {

    static std::atomic<bool> g_isStrpInited{false};
    static constexpr size_t MAX_PAYLOAD_SIZE = 1024;

    Stream::Stream(IOContext& context, const std::vector<uint8_t>& localKey, const std::vector<uint8_t>& remoteKey) : m_socket(context),
        m_context(context), m_sendSession(nullptr), m_recvSession(nullptr), m_localSSRC(0),
        m_remoteSSRC(0) {

        if (!g_isStrpInited.exchange(true)) {
            srtp_init();
        }

        m_sendSession = CreateSrtpSession(localKey, 0, ssrc_any_outbound);
        m_recvSession = CreateSrtpSession(remoteKey, 0, ssrc_any_inbound);
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

    void Stream::Send(const std::vector<uint8_t>& payload) {
        if (payload.size() > MAX_PAYLOAD_SIZE) {
            return;
        }

        Header header;
        BuildRtpHeader(&header, false);

        std::vector<uint8_t> outBuf(1500, '\0');
        std::memcpy(outBuf.data(), &header, sizeof(header));
        std::memcpy(outBuf.data() + sizeof(header), payload.data(), payload.size());

        int length = payload.size() + sizeof(header);
        srtp_protect(m_sendSession, outBuf.data(), &length);

        m_socket.send(asio::const_buffer(outBuf.data(), length));
    }

    void Stream::Send(const uint8_t* payload, const size_t size) {
        if (size > MAX_PAYLOAD_SIZE) {
            return;
        }

        Header header;
        BuildRtpHeader(&header, false);

        std::vector<uint8_t> outBuf(1500, '\0');
        std::memcpy(outBuf.data(), &header, sizeof(header));
        std::memcpy(outBuf.data() + sizeof(header), payload, size);

        int length = size + sizeof(header);
        srtp_protect(m_sendSession, outBuf.data(), &length);

        m_socket.send(asio::const_buffer(outBuf.data(), length));
    }

    asio::awaitable<void> Stream::AsyncSend(const uint8_t* payload, const size_t size) {
        if (size > MAX_PAYLOAD_SIZE) {
            co_return;
        }

        Header header;
        BuildRtpHeader(&header, false);

        std::vector<uint8_t> outBuf(1500, '\0');
        std::memcpy(outBuf.data(), &header, sizeof(header));
        std::memcpy(outBuf.data() + sizeof(header), payload, size);

        int length = size + sizeof(header);
        srtp_protect(m_sendSession, outBuf.data(), &length);

        co_await m_socket.async_send(asio::const_buffer(outBuf.data(), length));
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

    asio::awaitable<void> Stream::AsyncSend(const std::vector<uint8_t>& payload) {
        if (payload.size() > MAX_PAYLOAD_SIZE) {
            co_return;
        }

        Header header;
        BuildRtpHeader(&header, false);

        std::vector<uint8_t> outBuf(1500, '\0');
        std::memcpy(outBuf.data(), &header, sizeof(header));
        std::memcpy(outBuf.data() + sizeof(header), payload.data(), payload.size());

        int length = payload.size() + sizeof(header);
        srtp_protect(m_sendSession, outBuf.data(), &length);

        co_await m_socket.async_send(asio::const_buffer(outBuf.data(), length));
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


    void Stream::BuildRtpHeader(void* dst, const bool marker) {
        constexpr uint8_t PayloadType = 96;

        Header header{};
        header.v_p_x_cc = 0x80;
        header.m_pt = boost::endian::native_to_big(marker ? 0x80 : 0x00 | PayloadType & 0x7F);
        header.seq = boost::endian::native_to_big(m_sequence.fetch_add(1));
        header.timestamp = boost::endian::native_to_big(m_timestamp.fetch_add(1));
        header.ssrc = boost::endian::native_to_big(m_localSSRC);

        std::memcpy(dst, &header, sizeof(header));
    }
}
