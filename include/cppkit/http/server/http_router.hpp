#pragma once

#include "http_request.hpp"
#include "http_response.hpp"
#include <string>
#include <list>
#include <unordered_map>
#include <functional>
#include <memory>

namespace cppkit::http::server
{
    using HttpHandler = std::function<void(const HttpRequest&, HttpResponseWriter&)>;

    using NextFunc = std::function<void()>;

    using MiddlewareHandler = std::function<void(HttpRequest&, HttpResponseWriter&,
                                                 const NextFunc&)>;

    struct RouteNode
    {
        std::string segment;
        bool isParam = false;
        bool isWild = false;
        std::unordered_map<std::string, std::unique_ptr<RouteNode>> children;
        std::unordered_map<std::string, HttpHandler> handlers;
        std::list<MiddlewareHandler> middlewares{};
    };

    class Router
    {
    public:
        Router() : root(std::make_unique<RouteNode>())
        {
        }

        void addRoute(HttpMethod method, const std::string& path, const HttpHandler& handler);

        void addMiddleware(const std::string& path, const MiddlewareHandler& middleware);

        [[nodiscard]] bool exists(HttpMethod method, const std::string& path) const;

        [[nodiscard]] std::list<MiddlewareHandler> getMiddlewares(const std::string& path) const;

        [[nodiscard]] static RouteNode* findNode(RouteNode* node,
                                                 const std::vector<std::string>& parts,
                                                 size_t index,
                                                 std::unordered_map<std::string, std::string>& params,
                                                 std::list<MiddlewareHandler>* collectedMiddlewares);

        [[nodiscard]] HttpHandler find(HttpMethod method, const std::string& path) const;

        [[nodiscard]] HttpHandler find(HttpMethod method,
                                       const std::string& path,
                                       std::unordered_map<std::string, std::string>& params) const;

    private:
        static RouteNode* match(RouteNode* node,
                                const std::vector<std::string>& parts,
                                size_t index,
                                std::unordered_map<std::string, std::string>& params);

        std::unique_ptr<RouteNode> root;
    };
} // namespace cppkit::http
