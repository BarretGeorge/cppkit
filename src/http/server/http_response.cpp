#include "cppkit/http/http_response.hpp"
#include "cppkit/http/server/http_response.hpp"
#include <sstream>
#include <unistd.h>
#include <sys/socket.h>

#include "cppkit/define.hpp"

namespace cppkit::http::server
{
    void HttpResponseWriter::setStatusCode(const int statusCode)
    {
        this->statusCode = statusCode;
    }

    void HttpResponseWriter::setHeaders(const std::map<std::string, std::string>& headers)
    {
        this->headers = headers;
    }

    void HttpResponseWriter::setHeader(const std::string& key, const std::string& value)
    {
        this->headers[key] = value;
    }

    void HttpResponseWriter::setContentType(const std::string& contentType)
    {
        this->headers["Content-Type"] = contentType;
    }

    ssize_t HttpResponseWriter::write(const std::string& body)
    {
        return write(std::vector<uint8_t>(body.begin(), body.end()));
    }

    ssize_t HttpResponseWriter::write(const std::vector<uint8_t>& body)
    {
        ssize_t totalBytesSent = 0;
        std::ostringstream response;

        // 状态行：添加状态码描述
        const auto status = HTTP_STATUS_MAP.find(this->statusCode);
        const std::string statusDesc = status != HTTP_STATUS_MAP.end() ? status->second : "Unknown";
        response << "HTTP/1.1 " << this->statusCode << " " << statusDesc << "\r\n";

        // 响应头
        for (const auto& [key, value] : this->headers)
        {
            response << key << ": " << value << "\r\n";
        }
        response << "Content-Length: " << body.size() << "\r\n";
        response << "Connection: close\r\n"; // 关闭连接
        response << "\r\n";

        const std::string header_str = response.str();
        totalBytesSent += ::send(fd, header_str.data(), header_str.size(), 0);

        // 分批次发送响应体
        size_t bytesSent = 0;
        const size_t bodySize = body.size();
        while (bytesSent < bodySize)
        {
            const size_t chunkSize = std::min(static_cast<size_t>(DEFAULT_BUFFER_SIZE), bodySize - bytesSent);
            const ssize_t n = send(fd, &body[bytesSent], chunkSize, 0);
            if (n <= 0)
            {
                break;
            }
            bytesSent += n;
            totalBytesSent += n;
        }
        return totalBytesSent;
    }
}
