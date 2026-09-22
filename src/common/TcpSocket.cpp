#include "copilot/net/TcpSocket.hpp"

#include <array>
#include <cstring>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace copilot::net {
namespace {

#ifdef _WIN32
void ensureSocketRuntime() {
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)initialized;
}
SOCKET asSocket(NativeSocket value) { return static_cast<SOCKET>(value); }
void closeNative(NativeSocket socket) { if (socket != kInvalidSocket) closesocket(asSocket(socket)); }
#else
void ensureSocketRuntime() {}
int asSocket(NativeSocket value) { return value; }
void closeNative(NativeSocket socket) { if (socket != kInvalidSocket) ::close(socket); }
#endif

} // namespace

TcpStream::TcpStream(NativeSocket socket) : socket_(socket) { ensureSocketRuntime(); }
TcpStream::~TcpStream() { close(); }

TcpStream::TcpStream(TcpStream&& other) noexcept {
    socket_.store(other.socket_.exchange(kInvalidSocket));
    receiveBuffer_ = std::move(other.receiveBuffer_);
}

TcpStream& TcpStream::operator=(TcpStream&& other) noexcept {
    if (this != &other) {
        close();
        socket_.store(other.socket_.exchange(kInvalidSocket));
        receiveBuffer_ = std::move(other.receiveBuffer_);
    }
    return *this;
}

std::optional<TcpStream> TcpStream::connectLoopback(std::uint16_t port) {
    ensureSocketRuntime();
    const auto sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (sock == INVALID_SOCKET) {
#else
    if (sock < 0) {
#endif
        return std::nullopt;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        ::close(sock);
#endif
        return std::nullopt;
    }
    return TcpStream(static_cast<NativeSocket>(sock));
}

bool TcpStream::sendLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(sendMutex_);
    const auto native = socket_.load();
    if (native == kInvalidSocket) return false;
    std::string payload = line;
    payload.push_back('\n');
    std::size_t sentTotal = 0;
    while (sentTotal < payload.size()) {
        const auto remaining = payload.size() - sentTotal;
#ifdef _WIN32
        const int sent = ::send(asSocket(native), payload.data() + sentTotal, static_cast<int>(remaining), 0);
#else
        const auto sent = ::send(asSocket(native), payload.data() + sentTotal, remaining, 0);
#endif
        if (sent <= 0) return false;
        sentTotal += static_cast<std::size_t>(sent);
    }
    return true;
}

ReceiveResult TcpStream::receiveLine(int timeoutMs, std::size_t maxBytes) {
    if (!valid()) return {ReceiveStatus::Closed, {}};
    for (;;) {
        const auto newline = receiveBuffer_.find('\n');
        if (newline != std::string::npos) {
            std::string line = receiveBuffer_.substr(0, newline);
            receiveBuffer_.erase(0, newline + 1);
            return {ReceiveStatus::Line, std::move(line)};
        }
        if (receiveBuffer_.size() >= maxBytes) return {ReceiveStatus::TooLarge, {}};
        const auto native = socket_.load();
        if (native == kInvalidSocket) return {ReceiveStatus::Closed, {}};
        if (timeoutMs >= 0) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(asSocket(native), &readSet);
            timeval timeout{};
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_usec = (timeoutMs % 1000) * 1000;
#ifdef _WIN32
            const int ready = ::select(0, &readSet, nullptr, nullptr, &timeout);
#else
            const int ready = ::select(asSocket(native) + 1, &readSet, nullptr, nullptr, &timeout);
#endif
            if (ready == 0) return {ReceiveStatus::Timeout, {}};
            if (ready < 0) return {ReceiveStatus::Error, {}};
        }
        std::array<char, 4096> buffer{};
#ifdef _WIN32
        const int count = ::recv(asSocket(native), buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const auto count = ::recv(asSocket(native), buffer.data(), buffer.size(), 0);
#endif
        if (count == 0) return {ReceiveStatus::Closed, {}};
        if (count < 0) return {ReceiveStatus::Error, {}};
        receiveBuffer_.append(buffer.data(), static_cast<std::size_t>(count));
    }
}

void TcpStream::close() {
    const auto native = socket_.exchange(kInvalidSocket);
    if (native != kInvalidSocket) {
#ifdef _WIN32
        shutdown(asSocket(native), SD_BOTH);
#else
        shutdown(asSocket(native), SHUT_RDWR);
#endif
        closeNative(native);
    }
}

bool TcpStream::valid() const { return socket_.load() != kInvalidSocket; }
TcpListener::~TcpListener() { close(); }

bool TcpListener::bindLoopback(std::uint16_t port) {
    ensureSocketRuntime();
    close();
    const auto sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (sock == INVALID_SOCKET) {
#else
    if (sock < 0) {
#endif
        return false;
    }
    int enable = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enable), sizeof(enable));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || ::listen(sock, 1) != 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        ::close(sock);
#endif
        return false;
    }
    socket_.store(static_cast<NativeSocket>(sock));
    return true;
}

std::optional<TcpStream> TcpListener::acceptOne() {
    if (!valid()) return std::nullopt;
    const auto native = socket_.load();
    if (native == kInvalidSocket) return std::nullopt;
    const auto client = ::accept(asSocket(native), nullptr, nullptr);
#ifdef _WIN32
    if (client == INVALID_SOCKET) {
#else
    if (client < 0) {
#endif
        return std::nullopt;
    }
    return TcpStream(static_cast<NativeSocket>(client));
}

void TcpListener::close() {
    const auto native = socket_.exchange(kInvalidSocket);
    if (native != kInvalidSocket) {
#ifdef _WIN32
        shutdown(asSocket(native), SD_BOTH);
#else
        shutdown(asSocket(native), SHUT_RDWR);
#endif
        closeNative(native);
    }
}

bool TcpListener::valid() const { return socket_.load() != kInvalidSocket; }

} // namespace copilot::net
