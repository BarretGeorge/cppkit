#pragma once
#include <string>
#include <vector>
#include <unistd.h>
#include <cerrno>
#include "http_request.hpp"

namespace cppkit::http::server
{
    enum class ParseStatus
    {
        HeaderComplete, // header 解析完毕
        Incomplete, // Header 还没收全
        Error
    };

    class HttpContext
    {
    public:
        std::string recvBuffer; // 用于接收 Header 的缓冲区
        std::unique_ptr<HttpRequest> request; // 解析后的请求对象

        ParseStatus parse(const int fd)
        {
            if (request) return ParseStatus::HeaderComplete;

            char buffer[DEFAULT_BUFFER_SIZE];

            while (true)
            {
                const ssize_t len = read(fd, buffer, sizeof(buffer));

                if (len < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) return ParseStatus::Incomplete;
                    return ParseStatus::Error;
                }
                if (len == 0) return ParseStatus::Error; // 对端关闭

                recvBuffer.append(buffer, len);

                // 2. 检查 Header 结束标志
                constexpr std::string_view delimiter = "\r\n\r\n";
                if (const size_t pos = recvBuffer.find(delimiter); pos != std::string::npos)
                {
                    const size_t headerEnd = pos + delimiter.size();

                    const std::string headerRaw = recvBuffer.substr(0, headerEnd);

                    const std::string extraData = recvBuffer.substr(headerEnd);

                    request = std::make_unique<HttpRequest>(HttpRequest::parse(fd, headerRaw, extraData));

                    recvBuffer.clear();

                    return ParseStatus::HeaderComplete;
                }
            }
        }
    };
}
