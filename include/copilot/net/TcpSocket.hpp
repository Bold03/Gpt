#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace copilot::net {

#ifdef _WIN32
using NativeSocket = std::uintptr_t;
inline constexpr NativeSocket kInvalidSocket = static_cast<NativeSocket>(~0ULL);
#else
using NativeSocket = int;
inline constexpr NativeSocket kInvalidSocket = -1;
#endif

enum class ReceiveStatus { Line, Timeout, Closed, Error, TooLarge };

struct ReceiveResult {
    ReceiveStatus status{ReceiveStatus::Error};
    std::string line;
};

class TcpStream {
public:
    TcpStream() = default;
    explicit TcpStream(NativeSocket socket);
    ~TcpStream();
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    TcpStream(TcpStream&& other) noexcept;
    TcpStream& operator=(TcpStream&& other) noexcept;
    static std::optional<TcpStream> connectLoopback(std::uint16_t port);
    bool sendLine(const std::string& line);
    ReceiveResult receiveLine(int timeoutMs = -1, std::size_t maxBytes = 1024 * 1024);
    void close();
    bool valid() const;
private:
    std::atomic<NativeSocket> socket_{kInvalidSocket};
    std::string receiveBuffer_;
    std::mutex sendMutex_;
};

class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    bool bindLoopback(std::uint16_t port);
    std::optional<TcpStream> acceptOne();
    void close();
    bool valid() const;
private:
    std::atomic<NativeSocket> socket_{kInvalidSocket};
};

} // namespace copilot::net
