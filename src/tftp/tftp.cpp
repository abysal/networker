#include "./tftp.hpp"
#include <cstdint>
#include <iphlpapi.h>
#include <memory>
#include <minwinbase.h>
#include <print>
#include <ranges>
#include <stdexcept>
#include <windows.h>
#include <winerror.h>
#include <winsock.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

namespace tftp {

    TFTPServer::~TFTPServer() {
        WSACleanup();
        if (this->base_dispatch_socket != INVALID_SOCKET) {
            closesocket(this->base_dispatch_socket);
        }
    }

    SOCKET TFTPServer::allocate_socket(const std::optional<std::string>& port) {
        int result;

        const addrinfo hints = {
            .ai_flags    = AI_PASSIVE,
            .ai_family   = AF_INET,
            .ai_socktype = SOCK_DGRAM,
            .ai_protocol = IPPROTO_UDP,
        };

        addrinfo* addr_info;

        const auto* port_hint =
            port.and_then(
                    [](const auto& str) -> std::optional<const char*> { return str.data(); }
            ).value_or("0");

        result = getaddrinfo(nullptr, port_hint, &hints, &addr_info);

        if (result != 0) {
            throw std::runtime_error(
                std::format("Failed to get address info: {} error code", result)
            );
        }

        auto sock =
            socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);

        auto base_addr =
            std::unique_ptr<addrinfo, decltype(&FreeAddrInfo)>(addr_info, &FreeAddrInfo);

        if (sock == INVALID_SOCKET) {
            throw std::runtime_error(
                std::format("Failed to open socket: {} error", WSAGetLastError())
            );
        }

        result = bind(sock, base_addr->ai_addr, (int)base_addr->ai_addrlen);

        if (result != 0) {
            throw std::runtime_error(
                std::format("Failed to bind socket: {} error", WSAGetLastError())
            );
        }

        return sock;
    }

    std::vector<uint8_t> TFTPServer::read_file(const std::string& file_name) {
        return std::views::iota(1, 2059) | std::ranges::to<std::vector<uint8_t>>();
    }

    void TFTPServer::instance() {
        WSAData data;
        int     result = WSAStartup(MAKEWORD(2, 2), &data);

        if (result != 0) {
            throw std::runtime_error("Failed to init WinSock");
        }

        this->base_dispatch_socket = this->allocate_socket("25565");

        this->new_client_listen_loop();
    }

    void
    TFTPServer::handle_read_client(sockaddr* addr, int addr_size, ReadOpenConnection request) {
        auto client_socket = this->allocate_socket();
        int  timeout       = 5000;
        //setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(int));

        std::array<uint8_t, 1024> resource{};

        SocketInteraction socket{
            generic::BinaryBuffer(resource), client_socket, addr, addr_size
        };

        const auto data = this->read_file(request.filename);

        bool more_packets = true;

        TFTPDataSender sender{data};

        size_t drop_count = 0;

        while (more_packets) {
            const bool last_packet = sender.send_packet(socket);
            auto       data        = socket.recv_packet();

            if (!data.has_value()) {
                drop_count++;
                continue; // Act as if we never sent the last block of data
            }

            drop_count    = 0;
            auto raw_data = data.value();

            generic::BinaryBuffer current_packet_buffer{raw_data};

            const TFTPOpCode code = (TFTPOpCode)current_packet_buffer.read_next<uint16_t>();

            switch (code) {
            case TFTPOpCode::ACK: {

                const auto ack = current_packet_buffer.read_next<Ack>();

                if (sender.ack_data(ack.block)) {

                    if (last_packet) {
                        more_packets = false;
                    }

                    break;
                }

                auto err = ErrorMessage{
                    .code    = TFTPErrorCode::UNKNOWN_ID,
                    .message = std::format(
                        "Block ID did not match. Expected {}, got {}", sender.packet_id(),
                        ack.block
                    )
                };

                socket.write(std::move(err));
                socket.send();

                break;
            }

            default: {
                ErrorMessage err = ErrorMessage{
                    .code = TFTPErrorCode::UNKNOWN_TRANSACTION, .message = "Invalid opcode"
                };

                socket.write(std::move(err));
                socket.send();
                more_packets = false;
            }
            }
        }
    }

    // TODO: handle more than 65k blocks
    bool TFTPDataSender::send_packet(SocketInteraction& sender) {
        if (this->data.size() < 512) {
            sender.write(Data{.block = 1, .finished = true, .data = this->data});

            sender.send();
            return true;
        }

        const size_t end_index     = this->current_packet_id * 512;
        const size_t current_index = (this->current_packet_id - 1) * 512;

        if (end_index >= this->data.size()) {
            const auto data = this->data.subspan(current_index);

            sender.write(Data{.block = this->current_packet_id, .finished = true, .data = data}
            );

            sender.send();
            return true;
        }

        const auto data = this->data.subspan(current_index, 512);

        sender.write(Data{.block = this->current_packet_id, .finished = false, .data = data});

        sender.send();

        return false;
    }

    void TFTPServer::new_client_listen_loop() {

        while (true) {
            sockaddr_storage addr;

            socklen_t size = sizeof(addr);

            auto result = recvfrom(
                this->base_dispatch_socket, this->client_accept_buffer.data(),
                this->client_accept_buffer.size(), 0, (sockaddr*)&addr, &size
            );

            if (result == SOCKET_ERROR) {
                const auto error = WSAGetLastError();

                if (error == WSAEMSGSIZE) {
                    std::println("Got a packet too large to handle, Skipping!");
                    continue;
                }

                throw std::runtime_error(std::format("Error when listening: {} error", error));
            };

            generic::BinaryBuffer buff(
                std::span((uint8_t*)this->client_accept_buffer.data(), result)
            );

            const auto type = static_cast<TFTPOpCode>(buff.read_next<uint16_t>());

            switch (type) {
            case TFTPOpCode::WRQ:
                throw std::logic_error("Not implemented");
                break;
            case TFTPOpCode::RRQ: {

                auto roc = buff.read_next<ReadOpenConnection>();

                this->handle_read_client((sockaddr*)&addr, size, std::move(roc));

                break;
            }
            default:
                continue;
            };
        }
    }

} // namespace tftp