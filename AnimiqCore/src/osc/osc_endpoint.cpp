#include "animiq/osc/osc_endpoint.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Ws2tcpip.h>
#include <WinSock2.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace animiq::osc {

namespace {

void WritePaddedString(std::vector<std::uint8_t>& out, const std::string& value) {
    out.insert(out.end(), value.begin(), value.end());
    out.push_back(0U);
    while ((out.size() % 4U) != 0U) {
        out.push_back(0U);
    }
}

std::vector<std::uint8_t> BuildOscFloatPacket(const std::string& address, float value) {
    std::vector<std::uint8_t> packet;
    packet.reserve(64U);
    WritePaddedString(packet, address.empty() ? "/Animiq/Value" : address);
    WritePaddedString(packet, ",f");

    std::uint32_t raw = 0U;
    static_assert(sizeof(float) == sizeof(std::uint32_t), "float size mismatch");
    std::memcpy(&raw, &value, sizeof(raw));
    const std::array<std::uint8_t, 4> be = {
        static_cast<std::uint8_t>((raw >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((raw >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((raw >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(raw & 0xFFU),
    };
    packet.insert(packet.end(), be.begin(), be.end());
    return packet;
}

bool ParseHostPort(const std::string& input, std::string* out_host, std::uint16_t* out_port) {
    if (out_host == nullptr || out_port == nullptr) {
        return false;
    }
    if (input.empty()) {
        return true;
    }
    const std::size_t pos = input.find_last_of(':');
    if (pos == std::string::npos) {
        *out_host = input;
        return true;
    }

    const std::string host = input.substr(0U, pos);
    const std::string port_token = input.substr(pos + 1U);
    if (!host.empty()) {
        *out_host = host;
    }
    if (port_token.empty()) {
        return true;
    }
    int port = 0;
    try {
        port = std::stoi(port_token);
    } catch (...) {
        return false;
    }
    if (port < 1 || port > 65535) {
        return false;
    }
    *out_port = static_cast<std::uint16_t>(port);
    return true;
}

}  // namespace

bool OscEndpoint::SetDestination(const std::string& host_port) {
    std::string host = remote_host_;
    std::uint16_t port = remote_port_;
    if (!ParseHostPort(host_port, &host, &port)) {
        return false;
    }
    if (host.empty()) {
        host = "127.0.0.1";
    }
    remote_host_ = host;
    remote_port_ = port;
    return true;
}

bool OscEndpoint::Bind(std::uint16_t port) {
#if !defined(_WIN32)
    (void)port;
    return false;
#else
    Close();

    WSADATA wsa_data {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }
    wsa_started_ = true;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        Close();
        return false;
    }

    sockaddr_in local_addr {};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<const sockaddr*>(&local_addr), sizeof(local_addr)) != 0) {
        closesocket(sock);
        Close();
        return false;
    }

    socket_handle_ = static_cast<std::uintptr_t>(sock);
    bound_ = true;
    port_ = port;
    return true;
#endif
}

bool OscEndpoint::Publish(const std::string& address, float value) {
#if !defined(_WIN32)
    (void)address;
    (void)value;
    return false;
#else
    if (!bound_ || socket_handle_ == static_cast<std::uintptr_t>(~0ULL)) {
        return false;
    }
    const auto packet = BuildOscFloatPacket(address, value);

    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* resolved = nullptr;
    const std::string service = std::to_string(remote_port_);
    if (getaddrinfo(remote_host_.c_str(), service.c_str(), &hints, &resolved) != 0 || resolved == nullptr) {
        if (resolved != nullptr) {
            freeaddrinfo(resolved);
        }
        return false;
    }

    SOCKET sock = static_cast<SOCKET>(socket_handle_);
    const int sent = sendto(
        sock,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        resolved->ai_addr,
        static_cast<int>(resolved->ai_addrlen));
    freeaddrinfo(resolved);
    return sent == static_cast<int>(packet.size());
#endif
}

void OscEndpoint::Close() {
#if defined(_WIN32)
    if (socket_handle_ != static_cast<std::uintptr_t>(~0ULL)) {
        closesocket(static_cast<SOCKET>(socket_handle_));
        socket_handle_ = static_cast<std::uintptr_t>(~0ULL);
    }
    if (wsa_started_) {
        WSACleanup();
        wsa_started_ = false;
    }
#endif
    bound_ = false;
    port_ = 0;
}

bool OscEndpoint::IsBound() const {
    return bound_;
}

}  // namespace animiq::osc
