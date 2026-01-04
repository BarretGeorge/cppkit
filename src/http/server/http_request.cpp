#include "cppkit/http/server/http_request.hpp"
#include <unistd.h>
#include <sstream>
#include <thread>
#include <algorithm>
#include <ranges>

#include "cppkit/strings.hpp"

namespace cppkit::http::server
{
    HttpRequest HttpRequest::parse(const int fd, const std::string& raw, const std::string& extra_data)
    {
        HttpRequest request{fd};

        if (!extra_data.empty())
        {
            request.extraData.reserve(extra_data.size());
            request.extraData.assign(extra_data.begin(), extra_data.end());
        }

        std::string_view raw_view(raw);
        size_t cursor = 0;
        const size_t length = raw_view.length();

        const size_t line_end = raw_view.find("\r\n", cursor);
        if (line_end == std::string_view::npos)
            return request; // 格式错误或不完整

        std::string_view request_line = raw_view.substr(cursor, line_end - cursor);
        cursor = line_end + 2; // 跳过 \r\n

        size_t method_end = request_line.find(' ');
        if (method_end != std::string_view::npos)
        {
            std::string_view method_str = request_line.substr(0, method_end);

            if (method_str == "GET")
                request.method = HttpMethod::Get;
            else if (method_str == "POST")
                request.method = HttpMethod::Post;
            else if (method_str == "PUT")
                request.method = HttpMethod::Put;
            else if (method_str == "DELETE")
                request.method = HttpMethod::Delete;

            size_t uri_start = method_end + 1;
            size_t uri_end = request_line.find(' ', uri_start);
            if (uri_end != std::string_view::npos)
            {
                std::string_view uri = request_line.substr(uri_start, uri_end - uri_start);

                size_t q_pos = uri.find('?');
                if (q_pos != std::string_view::npos)
                {
                    // path
                    request.path = std::string(uri.substr(0, q_pos));

                    // query
                    std::string_view query_str = uri.substr(q_pos + 1);
                    size_t q_cursor = 0;
                    while (q_cursor < query_str.length())
                    {
                        size_t amp_pos = query_str.find('&', q_cursor);
                        std::string_view pair = query_str.substr(q_cursor, amp_pos - q_cursor);

                        if (amp_pos != std::string_view::npos)
                            q_cursor = amp_pos + 1;
                        else
                            q_cursor = query_str.length();

                        size_t eq_pos = pair.find('=');
                        if (eq_pos != std::string_view::npos)
                        {
                            std::string key(pair.substr(0, eq_pos));
                            std::string value(pair.substr(eq_pos + 1));
                            // url解码
                            request.query[std::move(urlDecode(key))].push_back(std::move(urlDecode(value)));
                        }
                    }
                }
                else
                {
                    request.path = std::string(uri);
                }
            }
        }

        while (cursor < length)
        {
            const size_t next_line_end = raw_view.find("\r\n", cursor);
            if (next_line_end == std::string_view::npos)
                break;

            // 空行 (\r\n\r\n) 表示 Header 结束，Body 开始
            if (next_line_end == cursor)
            {
                cursor += 2; // 跳过最后的空行
                break;
            }

            std::string_view header_line = raw_view.substr(cursor, next_line_end - cursor);
            cursor = next_line_end + 2;

            if (const size_t colon_pos = header_line.find(':'); colon_pos != std::string_view::npos)
            {
                const std::string key(header_line.substr(0, colon_pos));
                std::string_view value_view = header_line.substr(colon_pos + 1);
                while (!value_view.empty() && (value_view.front() == ' ' || value_view.front() == '\t'))
                {
                    value_view.remove_prefix(1);
                }
                // HTTP 协议标准（RFC 7230）明确规定Header字段名是大小写不敏感
                // 统一转换为小写以简化后续处理
                request.headers[std::move(toLower(key))].emplace_back(value_view);
            }
        }
        return request;
    }

    HttpMethod HttpRequest::getMethod() const
    {
        return method;
    }

    std::vector<u_int8_t> HttpRequest::readBody() const
    {
        if (readBodyFlag)
        {
            return _body;
        }

        size_t contentLength = 0;
        try
        {
            if (const std::string lenStr = getHeader("Content-Length"); !lenStr.empty())
            {
                contentLength = std::stoul(lenStr);
            }
        }
        catch (...)
        {
            contentLength = 0;
        }

        // 不可以超过内存限制
        if (contentLength > MAX_BODY_SIZE)
        {
            throw std::runtime_error("Request body too large.");
        }

        if (contentLength > 0)
        {
            _body.reserve(contentLength + extraData.size());
            _body.insert(_body.end(), extraData.begin(), extraData.end());
            size_t remaining = contentLength - extraData.size();
            char buffer[DEFAULT_BUFFER_SIZE];
            while (remaining > 0)
            {
                const ssize_t len = read(this->_fd, buffer, std::min(DEFAULT_BUFFER_SIZE, static_cast<int>(remaining)));
                if (len <= 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        // 使用readBody需要一次性读取完毕，遇到EAGAIN则让出时间片重试
                        // 后续可以使用协程或者异步回调的方式优化
                        std::this_thread::yield();
                        continue;
                    }
                    throw std::runtime_error("Failed to read body from socket.");
                }
                _body.insert(_body.end(), buffer, buffer + len);
                remaining -= len;
            }
        }
        readBodyFlag = true;
        return _body;
    }

    size_t HttpRequest::readBody(char* buffer, const size_t maxLen, size_t offset) const
    {
        if (readBodyFlag) // 是否已经读取完毕了
        {
            if (offset >= _body.size())
                return 0;
            const size_t readLen = std::min(maxLen, _body.size() - offset);
            std::copy_n(_body.begin() + static_cast<long>(offset), readLen, buffer);
            return readLen;
        }

        size_t contentLength = 0;
        try
        {
            if (const std::string lenStr = getHeader("content-length"); !lenStr.empty())
            {
                contentLength = std::stoul(lenStr);
            }
        }
        catch (...)
        {
            return 0; // 解析失败或没有长度，无法处理流式读取
        }

        // 如果 offset 已经超过了总长度，说明读完了
        if (offset >= contentLength)
        {
            return 0;
        }

        size_t totalBytesCopied = 0;
        char* currentBufPtr = buffer;
        size_t currentMaxLen = maxLen;

        // 处理extraData数据
        if (offset < extraData.size())
        {
            // 计算extraData中可用的数据量
            const size_t availableInExtra = extraData.size() - offset;
            const size_t toCopy = std::min(currentMaxLen, availableInExtra);

            std::copy_n(extraData.data() + static_cast<long>(offset), toCopy, currentBufPtr);

            totalBytesCopied += toCopy;
            currentBufPtr += toCopy;
            currentMaxLen -= toCopy;

            // 更新offset
            offset += toCopy;
        }

        // 是否已经填满buffer
        if (currentMaxLen == 0)
        {
            return totalBytesCopied;
        }

        const size_t remainingTotal = contentLength - offset;

        // 计算还需要从socket读取的字节数
        const size_t bytesToReadFromSocket = std::min(currentMaxLen, remainingTotal);

        if (bytesToReadFromSocket > 0)
        {
            if (const ssize_t ret = read(this->_fd, currentBufPtr, bytesToReadFromSocket); ret > 0)
            {
                totalBytesCopied += ret;
            }
            else if (ret == 0) // EOF
            {
                return totalBytesCopied;
            }
            else
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // 数据未准备好，返回已读取的字节数
                    return totalBytesCopied;
                }
                throw std::runtime_error("Socket read error");
            }
        }

        return totalBytesCopied;
    }

    void HttpRequest::resetBody(const std::vector<uint8_t>& body) const
    {
        readBodyFlag = true;
        _body = body;
    }

    void HttpRequest::parseFormData() const
    {
        // 获取请求格式
        if (const std::string contentType = getHeader("Content-Type");
            contentType.find("application/x-www-form-urlencoded") == std::string::npos)
        {
            return;
        }
        auto body = readBody();
        std::istringstream stream(std::string(body.begin(), body.end()));
        std::string pair;
        while (std::getline(stream, pair, '&'))
        {
            if (size_t eqpos = pair.find('='); eqpos != std::string::npos)
            {
                std::string key = pair.substr(0, eqpos);
                std::string value = pair.substr(eqpos + 1);
                formData.at(key).push_back(value);
            }
        }
    }

    std::string HttpRequest::getPath() const
    {
        return path;
    }

    std::string HttpRequest::getParam(const std::string& key) const
    {
        return _params.at(key);
    }

    std::unordered_map<std::string, std::string> HttpRequest::getParams() const
    {
        return _params;
    }

    std::string HttpRequest::getHeader(const std::string& key) const
    {
        if (const auto it = headers.find(toLower(key)); it != headers.end() && !it->second.empty())
            return it->second[0];
        return "";
    }

    std::map<std::string, std::vector<std::string>> HttpRequest::getHeaders() const
    {
        return headers;
    }

    std::string HttpRequest::getQuery(const std::string& key) const
    {
        if (const auto it = query.find(key); it != query.end() && !it->second.empty())
            return it->second[0];
        return "";
    }

    std::map<std::string, std::vector<std::string>> HttpRequest::getQuerys() const
    {
        return query;
    }

    void HttpRequest::setQuerys(const std::map<std::string, std::vector<std::string>>& querys)
    {
        this->query = querys;
    }

    void HttpRequest::setHeaders(const std::map<std::string, std::vector<std::string>>& headers)
    {
        this->headers = headers;
    }

    void HttpRequest::setQuery(const std::string& key, const std::vector<std::string>& values)
    {
        this->query[key] = values;
    }

    void HttpRequest::setQuery(const std::string& key, const std::string& value)
    {
        this->query[key] = {value};
    }

    void HttpRequest::addQuery(const std::string& key, const std::string& value)
    {
        auto& it = this->query[key];
        it.push_back(value);
    }

    void HttpRequest::addHeader(const std::string& key, const std::string& value)
    {
        auto& it = this->headers[key];
        it.push_back(value);
    }

    void HttpRequest::setHeader(const std::string& key, const std::string& value)
    {
        this->headers[key] = {value};
    }

    void HttpRequest::setHeader(const std::string& key, const std::vector<std::string>& values)
    {
        this->headers[key] = values;
    }

    void HttpRequest::appendBody(const char* data, const size_t len) const
    {
        if (len == 0)
            return;
        _body.reserve(_body.size() + len);
        _body.insert(_body.end(), data, data + len);
        readBodyFlag = true;
    }

    void HttpRequest::setParams(std::unordered_map<std::string, std::string> params) const
    {
        this->_params = std::move(params);
    }
} // namespace cppkit::http::server
