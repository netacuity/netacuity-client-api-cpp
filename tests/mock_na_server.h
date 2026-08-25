/*
 * Copyright 2026 Digital Envoy, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * mock_na_server.h
 *   Thread-based UDP mock server for testing the XML NetAcuity protocol.
 *   Binds to port 0 so the OS assigns a free ephemeral port (avoiding
 *   flaky collisions with anything already bound to the real NetAcuity
 *   protocol port, 5400) and responds to every received datagram with a
 *   pre-configured byte sequence. Call port() after start() to discover
 *   which port was actually assigned, and point the class under test at it
 *   via its test-only port-override constructor/setter.
 *
 * Usage:
 *   MockNaServer server;
 *   if (!server.start()) { ... handle bind failure ... }
 *   server.setResponse(MockNaServer::makeXMLPacket("<response .../>"));
 *   // ... run query under test, targeting server.port() ...
 *   server.stop();  // also called in destructor
 */
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define MOCK_CLOSESOCKET close
typedef int MOCK_SOCKET;
#else
#include <WS2tcpip.h>
#include <windows.h>
#define MOCK_CLOSESOCKET closesocket
typedef SOCKET MOCK_SOCKET;

namespace {
// The test binary (unlike the example programs) never calls WSAStartup, so
// on Windows every socket()/bind() call fails with WSANOTINITIALISED before
// MockNaServer even gets a chance to bind. Initialize Winsock once per test
// translation unit via a static-duration object so it's ready before any
// TEST/TEST_F body runs, and clean up at process exit.
struct MockNaServerWinsockInitializer {
    MockNaServerWinsockInitializer() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    ~MockNaServerWinsockInitializer() {
        WSACleanup();
    }
};
static MockNaServerWinsockInitializer g_mockNaServerWinsockInitializer;
}  // namespace
#endif

class MockNaServer {
public:
    MockNaServer() : sockfd_(-1), boundPort_(0), running_(false), ready_(false) {}
    ~MockNaServer() { stop(); }

    // Not copyable
    MockNaServer(const MockNaServer&) = delete;
    MockNaServer& operator=(const MockNaServer&) = delete;

    // -----------------------------------------------------------------------
    // Packet builders
    // -----------------------------------------------------------------------

    // XML protocol: single-packet response.
    // Packet = 4 ASCII-digit header "0101" (packet 1 of 1) + XML content.
    static std::vector<uint8_t> makeXMLPacket(const std::string& xmlContent) {
        std::string s = "0101" + xmlContent;
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    // Configure the response that will be sent for every incoming datagram.
    void setResponse(const std::vector<uint8_t>& pkt) {
        std::lock_guard<std::mutex> lk(rspMtx_);
        response_ = pkt;
    }

    // Bind to an OS-assigned ephemeral port (port 0) and start the
    // background server thread. Use port() afterward to discover which
    // port was actually assigned. Returns false only on genuine socket
    // setup failure (binding to port 0 should never fail due to the port
    // being "in use", since the OS picks a free one).
    bool start() {
        sockfd_ = static_cast<int>(socket(AF_INET, SOCK_DGRAM, 0));
        if (sockfd_ == -1) return false;

        int opt = 1;
        setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&opt), sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(0);   // 0 = let the OS pick a free port
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sockfd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            MOCK_CLOSESOCKET(sockfd_);
            sockfd_ = -1;
            return false;
        }

        // Read back the port the OS actually assigned us.
        struct sockaddr_in boundAddr{};
        socklen_t boundAddrLen = sizeof(boundAddr);
        if (getsockname(sockfd_, reinterpret_cast<struct sockaddr*>(&boundAddr),
                         &boundAddrLen) < 0) {
            MOCK_CLOSESOCKET(sockfd_);
            sockfd_ = -1;
            return false;
        }
        boundPort_ = ntohs(boundAddr.sin_port);

        running_ = true;
        ready_   = false;
        thread_  = std::thread(&MockNaServer::loop, this);

        // Wait until the thread has entered its receive loop.
        std::unique_lock<std::mutex> lk(readyMtx_);
        readyCv_.wait(lk, [this] { return ready_.load(); });
        return true;
    }

    void stop() {
        running_ = false;
        if (sockfd_ != -1) {
            MOCK_CLOSESOCKET(sockfd_);
            sockfd_ = -1;
        }
        if (thread_.joinable()) thread_.join();
    }

    // Returns the ephemeral port assigned by the OS after a successful
    // start(). Only meaningful after start() has returned true.
    int port() const { return boundPort_; }

private:
    int                       sockfd_;
    int                       boundPort_;
    std::atomic<bool>         running_;
    std::atomic<bool>         ready_;
    std::thread               thread_;
    std::vector<uint8_t>      response_;
    std::mutex                rspMtx_;
    std::mutex                readyMtx_;
    std::condition_variable   readyCv_;

    void loop() {
        // Signal that we are ready before entering the blocking loop.
        {
            std::lock_guard<std::mutex> lk(readyMtx_);
            ready_ = true;
        }
        readyCv_.notify_all();

        char buf[4096];
        while (running_) {
            struct sockaddr_in clientAddr{};
            socklen_t addrLen = sizeof(clientAddr);

            // Poll with a 100 ms timeout so we can check running_ promptly.
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sockfd_, &fds);
            struct timeval tv{0, 100000};

            int sel = select(sockfd_ + 1, &fds, nullptr, nullptr, &tv);
            if (sel <= 0) continue;

            int n = recvfrom(sockfd_, buf, sizeof(buf), 0,
                                 reinterpret_cast<struct sockaddr*>(&clientAddr),
                                 &addrLen);
            if (n <= 0) continue;

            std::lock_guard<std::mutex> lk(rspMtx_);
            if (!response_.empty()) {
                sendto(sockfd_,
                       reinterpret_cast<const char*>(response_.data()),
                       static_cast<int>(response_.size()),
                       0,
                       reinterpret_cast<struct sockaddr*>(&clientAddr),
                       addrLen);
            }
        }
    }
};
