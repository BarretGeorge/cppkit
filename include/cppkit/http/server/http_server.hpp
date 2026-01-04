#pragma once

#include "http_request.hpp"
#include "http_router.hpp"
#include "http_response.hpp"
#include "http_context.hpp"
#include "cppkit/event/server.hpp"
#include <string>
#include <functional>
#include <filesystem>

namespace cppkit::http::server
{
    class GrouterGroup;

    class HttpServer
    {
    public:
        explicit HttpServer(std::string host = "localhost", const int port = 80) : _port(port), _host(std::move(host))
        {
        }

        void start();

        void stop();

        void Get(const std::string& path, const HttpHandler& handler);

        void Post(const std::string& path, const HttpHandler& handler);

        void Put(const std::string& path, const HttpHandler& handler);

        void Delete(const std::string& path, const HttpHandler& handler);

        GrouterGroup group(const std::string& prefix);

        ~HttpServer() = default;

        void setPort(const int port) { _port = port; }

        [[nodiscard]] int getPort() const { return _port; }

        [[nodiscard]] std::string getHost() const { return _host; }

        void setHost(std::string host) { _host = std::move(host); }

        void setMaxFileSize(uintmax_t size);

        [[nodiscard]] uintmax_t getMaxFileSize() const { return _maxFileSize; }

        void addMiddleware(const std::string& path, const MiddlewareHandler& middleware);

        void setStaticDir(std::string_view path, std::string_view dir);

    private:
        // 添加路由处理函数
        void addRoute(HttpMethod method, const std::string& path, const HttpHandler& handler);

        // 处理HTTP请求
        void handleRequest(HttpRequest& request, HttpResponseWriter& writer, int writerFd) const;

        // 静态文件处理
        bool staticHandler(const HttpRequest& request, HttpResponseWriter& writer, int writerFd) const;

        static size_t sendFile(int fd, const std::filesystem::path& filePath, uintmax_t fileSize);

        int _port;
        std::string _host;
        Router _router;
        Router _middleware;
        event::EventLoop _loop{};
        event::TcpServer _server{};
        std::string _staticPath; // 静态文件URL路径前缀
        std::string _staticDir; // 静态文件目录
        uintmax_t _maxFileSize{50 * 1024 * 1024}; // 50 MB
        std::unordered_map<int, HttpContext> contexts;
    };
} // namespace cppkit::http
