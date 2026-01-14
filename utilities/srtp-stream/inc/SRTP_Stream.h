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
        Stream(IOContext& context, srtp_t sendSession, srtp_t recvSession);

        void Bind(const UDPEndpoint& endpoint);
        void Bind(UDPEndpoint&& endpoint);

        void Receive(std::vector<uint8_t>& payload);
        void Send(const std::vector<uint8_t>& payload);

        asio::awaitable<void> AsyncReceive(std::vector<uint8_t>& payload);
        asio::awaitable<void> AsyncSend(const std::vector<uint8_t>& payload);

    private:
        void BuildRtpHeader(void* dst, bool marker);

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
