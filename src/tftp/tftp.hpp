#pragma once

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include "../binary_buffer.hpp"

#include <array>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

#include <winsock2.h>

#include <ws2tcpip.h>

namespace tftp {

    class SocketInteraction {
    public:
        SocketInteraction(
            generic::BinaryBuffer buffer, SOCKET sock, sockaddr* target_address, int sock_size
        )
            : buffer(buffer), socket(sock), target_address(target_address),
              socket_size(sock_size), underlying_buffer_size(buffer.size()) {};

        ~SocketInteraction() { closesocket(this->socket); }

        inline int send() {
            return sendto(
                this->socket, (const char*)buffer.raw(), buffer.clear(), 0,
                this->target_address, this->socket_size
            );
        }

        template <typename T> void write(T&& val) { this->buffer.write(std::forward<T>(val)); }

        inline std::optional<std::span<uint8_t>> recv_packet() {
            sockaddr_storage sender_addr;
            socklen_t p = sizeof(sockaddr_storage);
            const auto       data = recvfrom(
                this->socket, (char*)this->buffer.raw(), this->underlying_buffer_size, 0,
                (sockaddr*)&sender_addr, &p
            );

            if (data == SOCKET_ERROR) {
                const auto err = WSAGetLastError();

                if (err == WSAETIMEDOUT) {
                    return std::nullopt;
                }

                throw std::runtime_error(std::format("Client Recv Error: {}", err));
            }

            std::span<uint8_t> span = this->buffer.underlying().subspan(0, data);

            this->buffer.clear();
            return span;
        }

    private:
        SOCKET                socket;
        sockaddr*             target_address;
        int                   socket_size;
        int                   underlying_buffer_size{};
        generic::BinaryBuffer buffer;
    };

    enum class TFTPOpCode : uint16_t { RRQ = 1, WRQ, DATA, ACK, ERR };

    enum class TFTPMode { OCTET, NETASCII };

    enum class TFTPErrorCode : uint16_t {
        MESSAGE,
        FILE_NOT_FOUND,
        ACCESS_VIOLATION,
        NO_SPACE,
        UNKNOWN_TRANSACTION,
        UNKNOWN_ID,
        ALREADY_EXISTS,
        NO_USER
    };

    struct ErrorMessage {
        TFTPErrorCode code;
        std::string   message{};
    };

    struct Data {
        uint16_t block;
        bool     finished = false; // tells us if this is the last block in the chain!
        const std::span<const uint8_t> data;
    };

    struct Ack {
        uint16_t block;
    };

    template <TFTPOpCode type> struct OpenConnection {
        std::string filename{};
        TFTPMode    mode{};
    };

    using ReadOpenConnection = OpenConnection<TFTPOpCode::RRQ>;

    class TFTPDataSender {
    public:
        TFTPDataSender(const std::span<const uint8_t> data_to_send) : data(data_to_send) {};

        bool send_packet(SocketInteraction& sender);

        inline bool ack_data(uint16_t id) {
            if (this->current_packet_id == id) {
                this->current_packet_id++;
                return true;
            } else return false;
        }

        inline uint16_t packet_id() const { return this->current_packet_id; }

    private:
        uint16_t                       current_packet_id{1};
        const std::span<const uint8_t> data;
    };

    class TFTPServer {
    public:
        TFTPServer() = default;

        ~TFTPServer();

        void instance();

        void new_client_listen_loop();

    private:
        SOCKET allocate_socket(const std::optional<std::string>& port = std::nullopt);

        void handle_read_client(sockaddr* addr, int addr_size, ReadOpenConnection request);

        std::vector<uint8_t> read_file(const std::string& file_name);

    private:
        SOCKET                base_dispatch_socket{INVALID_SOCKET};
        std::array<char, 384> client_accept_buffer{};
    };
} // namespace tftp

namespace generic {

    using namespace tftp;

    template <> struct BinaryDataInterface<ErrorMessage> {
        static void write(ErrorMessage&& value, BinaryBuffer& buffer) {
            buffer.write((uint16_t)TFTPOpCode::ERR);
            buffer.write((uint16_t)value.code);
            buffer.write(std::move(value.message));
        };

        static ErrorMessage read(BinaryBuffer& buffer) {

            ErrorMessage message;
            message.code    = (TFTPErrorCode)buffer.read_next<uint16_t>();
            message.message = buffer.read_next<std::string>();
            return message;
        }

        static size_t size(const std::optional<ErrorMessage>& message) {
            if (message.has_value()) {
                const ErrorMessage& err = message.value();

                return 4 + err.message.size() + 1; // size of type, code, message, null
            } else {
                return 3; // sizeof the type, code, and then a null message
            }
        }
    };

    template <> struct BinaryDataInterface<Data> {
        static void write(Data&& value, BinaryBuffer& buffer) {

            buffer.write((uint16_t)TFTPOpCode::DATA);
            buffer.write((uint16_t)value.block);

            for (const auto byte : value.data) {
                buffer.write_byte(byte);
            }
        }

        static Data read(BinaryBuffer&) {
            throw std::logic_error("Decoder not implemented yet!");
        }

        static size_t size(const std::optional<Data>& message) {
            if (message.has_value()) {
                return 2 + message.value().data.size(); // size of type, block id, message
            } else {
                return 2; // size of type, blockid
            }
        }
    };

    template <> struct BinaryDataInterface<Ack> {
        static void write(Ack&& value, BinaryBuffer& buffer) {
            buffer.write((uint16_t)TFTPOpCode::ACK);
            buffer.write((uint16_t)value.block);
        }

        static Ack read(BinaryBuffer& buffer) {
            return Ack{.block = buffer.read_next<uint16_t>()};
        }

        static size_t size(const std::optional<Ack>& message) { return 2; }
    };

    template <TFTPOpCode type> struct BinaryDataInterface<OpenConnection<type>> {
        static void write(OpenConnection<type>&&, BinaryBuffer& buffer) {
            throw std::logic_error("Attempted to serialze a request we dont send");
        };

        static OpenConnection<type> read(BinaryBuffer& buffer) {
            OpenConnection<type> connection;

            connection.filename = buffer.read_next<std::string>();

            auto mode_string = buffer.read_next<std::string>();
            std::transform(
                mode_string.begin(), mode_string.end(), mode_string.data(),
                [](unsigned char c) { return std::tolower(c); }
            );

            if (mode_string == "netascii") {
                connection.mode = TFTPMode::NETASCII;
            } else if (mode_string == "octet") {
                connection.mode = TFTPMode::OCTET;
            } else {
                throw std::runtime_error("Unknown mode!");
            }

            return connection;
        }

        static size_t size(const std::optional<OpenConnection<type>>& connection) {
            if (connection.has_value()) {
                throw std::logic_error("This should never be populated");
            } else {
                return 2;
            }
        }
    };
} // namespace generic