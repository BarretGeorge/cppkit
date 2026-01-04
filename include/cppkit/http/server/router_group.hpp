#pragma once

#include "http_server.hpp"
#include "http_router.hpp"

namespace cppkit::http::server
{
    class GrouterGroup
    {
    public:
        GrouterGroup(HttpServer* server, std::string prefix)
            : _server(server), _prefix(std::move(prefix))
        {
        }

        void Get(const std::string& path, const HttpHandler& handler) const
        {
            _server->Get(_prefix + path, handler);
        }

        void Post(const std::string& path, const HttpHandler& handler) const
        {
            _server->Post(_prefix + path, handler);
        }

        void Put(const std::string& path, const HttpHandler& handler) const
        {
            _server->Put(_prefix + path, handler);
        }

        void Delete(const std::string& path, const HttpHandler& handler) const
        {
            _server->Delete(_prefix + path, handler);
        }

        template <typename... MiddlewareHandler>
        void use(MiddlewareHandler... func) const
        {
            (_server->addMiddleware(_prefix, func), ...);
        }

    private:
        HttpServer* _server;
        std::string _prefix;
    };
}
