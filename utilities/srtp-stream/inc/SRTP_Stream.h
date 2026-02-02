#ifndef SRTP_STREAM_H
#define SRTP_STREAM_H

#include <atomic>
#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <srtp2/srtp.h>
#include <AsioCommon.h>

namespace SRTP {
    struct Header {
        uint8_t v_p_x_cc;
        uint8_t m_pt;
        uint16_t seq;
        uint32_t timestamp;
        uint32_t ssrc;
    };

    class Stream final {
    public:
        Stream() = delete;
        Stream(IOContext& context, const std::vector<uint8_t>& localKey, const std::vector<uint8_t>& remoteKey);

        void Bind(const UDPEndpoint& endpoint);
        void Bind(UDPEndpoint&& endpoint);
        UDPEndpoint Bind();

        void Receive(std::vector<uint8_t>& payload);
        void Send(const std::vector<uint8_t>& payload);
        void Send(const uint8_t* payload, size_t size);

        asio::awaitable<void> AsyncReceive(std::vector<uint8_t>& payload);
        asio::awaitable<void> AsyncSend(const std::vector<uint8_t>& payload);
        asio::awaitable<void> AsyncSend(const uint8_t* payload, size_t size);

        static std::vector<uint8_t> GenerateKey();

    private:
        void BuildRtpHeader(void* dst, bool marker);
        static srtp_t CreateSrtpSession(const std::vector<uint8_t>& key, uint32_t ssrc, srtp_ssrc_type_t type);

        UDPSocket m_socket;
        IOContext& m_context;

        srtp_t m_sendSession;
        srtp_t m_recvSession;

        uint32_t m_localSSRC;
        uint32_t m_remoteSSRC;

        std::atomic<uint16_t> m_sequence{0};
        std::atomic<uint32_t> m_timestamp{0};
    };
}

#endif //SRTP_STREAM_H
