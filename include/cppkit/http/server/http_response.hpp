#pragma once

#include "cppkit/http/http_request.hpp"
#include <map>

namespace cppkit::http::server
{
  class HttpResponseWriter
  {
  public:
    explicit HttpResponseWriter(const int fd) : fd(fd)
    {
    }

    void setStatusCode(int statusCode);

    void setHeaders(const std::map<std::string, std::string>& headers);

    void setHeader(const std::string& key, const std::string& value);

    void setContentType(const std::string& contentType);

    ssize_t write(const std::string& body);

    ssize_t write(const std::vector<uint8_t>& body);

  private:
    int fd{};
    int statusCode{HTTP_OK};
    std::map<std::string, std::string> headers;
  };
} // namespace cppkit::http::server