#include "cppkit/event/server.hpp"
#include "cppkit/define.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace cppkit::event
{
    std::unordered_map<int, ConnInfo> connections_;

    static int setNonBlock(const int fd)
    {
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1)
        {
            return -1;
        }
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            return -1;
        }
        return 0;
    }

    TcpServer::TcpServer(EventLoop* loop, std::string addr, const uint16_t port) : loop_(loop), addr_(std::move(addr)),
        port_(port)
    {
    }

    TcpServer::~TcpServer()
    {
        if (listen_fd_ != -1)
        {
            close(listen_fd_);
        }
    }


    void TcpServer::start()
    {
        addrinfo hints{}, *res = nullptr;
        const addrinfo* rp = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

        const std::string portStr = std::to_string(port_);
        const char* node = addr_.empty() ? nullptr : addr_.c_str();

        if (const int gai = getaddrinfo(node, portStr.c_str(), &hints, &res); gai != 0)
        {
            throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(gai));
        }

        int sock{};
        bool bound = false;
        for (rp = res; rp != nullptr; rp = rp->ai_next)
        {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock < 0)
                continue;

            // 保留复用地址选项（确保重启时快速重绑）
            constexpr int on = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
            // 可选：在支持的平台上开启 SO_REUSEPORT
            setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
            // 若是 IPv6 socket，尝试允许双栈（可选）
            if (rp->ai_family == AF_INET6)
            {
                int off = 0; // 0 -> NOT v6 only，允许 IPv4-mapped if 内核允许
                setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
            }

            // 尝试 bind
            if (bind(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            {
                bound = true;
                break; // 成功
            }

            // bind 失败，关闭 socket 并继续尝试下一个地址
            close(sock);
        }

        freeaddrinfo(res);

        if (!bound)
        {
            throw std::runtime_error(std::string("bind: ") + strerror(errno));
        }

        // 成功绑定
        listen_fd_ = sock;

        if (listen(listen_fd_, SOMAXCONN) < 0)
        {
            close(listen_fd_);
            listen_fd_ = -1;
            throw std::runtime_error(std::string("listen: ") + strerror(errno));
        }

        setNonBlock(listen_fd_);

        // 创建监听事件
        loop_->createFileEvent(listen_fd_,
                               AE_READABLE,
                               [this](const int fd, int mask)
                               {
                                   while (true)
                                   {
                                       sockaddr_storage cli{};
                                       socklen_t cli_len = sizeof(cli);
                                       int c = accept(fd, reinterpret_cast<sockaddr*>(&cli), &cli_len);
                                       if (c < 0)
                                       {
                                           if (errno == EAGAIN || errno == EWOULDBLOCK)
                                               break;
                                           std::cerr << "accept error: " << strerror(errno) << "\n";
                                           break;
                                       }
                                       setNonBlock(c);

                                       char ipBuf[64];
                                       uint16_t port = 0;
                                       if (cli.ss_family == AF_INET)
                                       {
                                           inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&cli)->sin_addr, ipBuf,
                                                     sizeof(ipBuf));
                                           port = ntohs(reinterpret_cast<sockaddr_in*>(&cli)->sin_port);
                                       }
                                       else if (cli.ss_family == AF_INET6)
                                       {
                                           inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(&cli)->sin6_addr, ipBuf,
                                                     sizeof(ipBuf));
                                           port = ntohs(reinterpret_cast<sockaddr_in6*>(&cli)->sin6_port);
                                       }
                                       else
                                       {
                                           strcpy(ipBuf, "unknown");
                                       }

                                       connections_[c] = ConnInfo(
                                           ipBuf,
                                           port,
                                           c,
                                           [this](const ssize_t n, const int cfd)
                                           {
                                               this->cleanup(n, cfd);
                                           });

                                       if (onConn_)
                                       {
                                           onConn_(connections_[c]);
                                       }
                                       // create read handler for client
                                       loop_->createFileEvent(c,
                                                              AE_READABLE,
                                                              [this](const int cfd, int)
                                                              {
                                                                  ssize_t n;
                                                                  if (onReadable_)
                                                                  {
                                                                      n = onReadable_(connections_[cfd]);
                                                                      if (n < 0)
                                                                      {
                                                                          cleanup(n, cfd);
                                                                      }
                                                                      return;
                                                                  }
                                                                  char buf[DEFAULT_BUFFER_SIZE];
                                                                  if (n = read(cfd, buf, sizeof(buf)); n > 0)
                                                                  {
                                                                      if (onMsg_)
                                                                      {
                                                                          onMsg_(connections_[cfd],
                                                                              std::vector<uint8_t>(buf, buf + n));
                                                                      }
                                                                  }
                                                                  else
                                                                  {
                                                                      cleanup(n, cfd);
                                                                  }
                                                              });
                                   }
                               });
    }


    void TcpServer::stop()
    {
        loop_->deleteFileEvent(listen_fd_, AE_READABLE);
        if (listen_fd_ != -1)
        {
            close(listen_fd_);
        }
        listen_fd_ = -1;
    }

    void TcpServer::cleanup(const ssize_t n, const int cfd) const
    {
        // 检查是否是因为非阻塞无数据而忽略
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            return;
        }

        // 清理事件并关闭连接
        loop_->deleteFileEvent(cfd, AE_READABLE | AE_WRITABLE);
        close(cfd);
        if (onClose_)
        {
            onClose_(connections_[cfd]);
        }
        connections_.erase(cfd);
    }

    std::string ConnInfo::getIp() const
    {
        return this->ip;
    }

    uint16_t ConnInfo::getPort() const
    {
        return this->port;
    }

    std::string ConnInfo::getClientId() const
    {
        return this->ip + "@" + std::to_string(this->port);
    }

    ssize_t ConnInfo::send(const uint8_t* data, const size_t length) const
    {
        return ::send(this->fd, data, length, 0);
    }

    ssize_t ConnInfo::recv(uint8_t* data, const size_t length) const
    {
        return ::recv(this->fd, data, length, 0);
    }

    int ConnInfo::getFd() const
    {
        return this->fd;
    }

    void ConnInfo::close() const
    {
        if (this->cleanup)
        {
            this->cleanup(0, this->fd);
        }
        else
        {
            ::close(this->fd);
        }
    }

    bool ConnInfo::operator==(const ConnInfo& other) const noexcept
    {
        return ip == other.ip && port == other.port;
    }
} // namespace cppkit::event
