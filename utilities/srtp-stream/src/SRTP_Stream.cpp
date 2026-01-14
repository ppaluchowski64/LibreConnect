#include <SRTP_Stream.h>
#include <boost/endian.hpp>

namespace SRTP {

    static constexpr size_t MAX_PAYLOAD_SIZE = 1024;

    Stream::Stream(IOContext& context, const srtp_t sendSession, const srtp_t recvSession) : m_socket(context),
        m_context(context), m_sendSession(sendSession), m_recvSession(recvSession), m_localSSRC(0),
        m_remoteSSRC(0) {
    }

    void Stream::Bind(const UDPEndpoint& endpoint) {
        m_socket.bind(endpoint);
    }

    void Stream::Bind(UDPEndpoint&& endpoint) {
        m_socket.bind(std::forward<UDPEndpoint>(endpoint));
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

    asio::awaitable<void> Stream::AsyncReceive(std::vector<uint8_t>& payload) {
        std::vector<uint8_t> buffer(1500);

        auto [ec, bytesTransferred] = co_await m_socket.async_receive(
            asio::buffer(buffer),
            asio::use_awaitable
        );

        if (ec) co_return;
        int length = static_cast<int>(bytesTransferred);

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


    void Stream::BuildRtpHeader(void* dst, const bool marker) {
        constexpr uint8_t PayloadType = 96;

        Header header{};
        header.v_p_x_cc = boost::endian::native_to_big(0x80);
        header.m_pt = boost::endian::native_to_big(marker ? 0x80 : 0x00 | PayloadType & 0x7F);
        header.seq = boost::endian::native_to_big(m_sequence.fetch_add(1));
        header.timestamp = boost::endian::native_to_big(m_timestamp.fetch_add(1));
        header.ssrc = boost::endian::native_to_big(m_localSSRC);

        std::memcpy(dst, &header, sizeof(header));
    }
}
