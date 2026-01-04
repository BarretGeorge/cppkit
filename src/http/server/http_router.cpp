#include "cppkit/http/server/http_router.hpp"
#include "cppkit/http/http_request.hpp"
#include "cppkit/strings.hpp"
#include <ranges>

namespace cppkit::http::server
{
    void Router::addRoute(const HttpMethod method, const std::string& path, const HttpHandler& handler)
    {
        const auto parts = split(path, '/');
        RouteNode* node = root.get();

        for (const auto& part : parts)
        {
            if (part.empty())
            {
                continue;
            }
            std::string key = part;
            bool isParam = false, isWild = false;

            if (!part.empty() && part[0] == ':')
            {
                key = ":";
                isParam = true;
            }
            if (!part.empty() && part[0] == '*')
            {
                key = "*";
                isWild = true;
            }

            if (!node->children.contains(key))
            {
                node->children[key] = std::make_unique<RouteNode>();
                node->children[key]->segment = part;
                node->children[key]->isParam = isParam;
                node->children[key]->isWild = isWild;
            }
            node = node->children[key].get();
        }
        node->handlers[httpMethodValue(method)] = handler;
    }

    void Router::addMiddleware(const std::string& path, const MiddlewareHandler& middleware)
    {
        const auto parts = split(path, '/');
        RouteNode* node = root.get();

        for (const auto& part : parts)
        {
            if (part.empty())
            {
                continue;
            }
            std::string key = part;
            bool isParam = false, isWild = false;

            if (!part.empty() && part[0] == ':')
            {
                key = ":";
                isParam = true;
            }
            if (!part.empty() && part[0] == '*')
            {
                key = "*";
                isWild = true;
            }

            if (!node->children.contains(key))
            {
                node->children[key] = std::make_unique<RouteNode>();
                node->children[key]->segment = part;
                node->children[key]->isParam = isParam;
                node->children[key]->isWild = isWild;
            }
            node = node->children[key].get();
        }
        node->middlewares.push_back(middleware);
    }

    bool Router::exists(const HttpMethod method, const std::string& path) const
    {
        const auto parts = split(path, '/');
        std::unordered_map<std::string, std::string> params;
        const RouteNode* node = match(root.get(), parts, 0, params);
        return node && node->handlers.contains(httpMethodValue(method));
    }

    std::list<MiddlewareHandler> Router::getMiddlewares(const std::string& path) const
    {
        std::list<MiddlewareHandler> collected;
        const auto parts = split(path, '/');
        RouteNode* current_node = root.get();

        collected.insert(collected.end(), current_node->middlewares.begin(), current_node->middlewares.end());

        for (const auto& part : parts)
        {
            if (part.empty())
            {
                continue;
            }

            bool found = false;
            std::string key = part;
            // bool isParam = false, isWild = false;

            if (!part.empty() && part[0] == ':')
            {
                key = ":";
                // isParam = true;
            }
            else if (!part.empty() && part[0] == '*')
            {
                key = "*";
                // isWild = true;
            }

            if (current_node->children.contains(key))
            {
                current_node = current_node->children[key].get();
                collected.insert(collected.end(), current_node->middlewares.begin(), current_node->middlewares.end());
                found = true;
            }
            else
            {
                for (const auto& childPtr : current_node->children | std::views::values)
                {
                    if (childPtr->isParam || childPtr->isWild)
                    {
                        current_node = childPtr.get();
                        collected.insert(collected.end(), current_node->middlewares.begin(),
                                         current_node->middlewares.end());
                        found = true;
                        break;
                    }
                }
            }

            if (!found)
            {
                break;
            }

            if (current_node->isWild)
            {
                collected.insert(collected.end(), current_node->middlewares.begin(), current_node->middlewares.end());
                break;
            }
        }

        return collected;
    }

    HttpHandler Router::find(const HttpMethod method, const std::string& path) const
    {
        std::unordered_map<std::string, std::string> params;
        return find(method, path, params);
    }

    HttpHandler Router::find(const HttpMethod method,
                             const std::string& path,
                             std::unordered_map<std::string, std::string>& params) const
    {
        const auto parts = split(path, '/');
        if (const RouteNode* node = match(root.get(), parts, 0, params);
            node && node->handlers.contains(httpMethodValue(method)))
        {
            return node->handlers.at(httpMethodValue(method));
        }
        return nullptr;
    }

    RouteNode* Router::match(RouteNode* node,
                             const std::vector<std::string>& parts,
                             const size_t index,
                             std::unordered_map<std::string, std::string>& params)
    {
        if (index == parts.size() || node->isWild)
        {
            return node;
        }

        size_t currentIndex = index;
        while (currentIndex < parts.size() && parts[currentIndex].empty())
        {
            ++currentIndex;
        }

        if (currentIndex == parts.size() || node->isWild)
        {
            return node;
        }

        const std::string& part = parts[currentIndex];

        if (node->children.contains(part))
        {
            RouteNode* child = node->children[part].get();
            if (RouteNode* result = match(child, parts, currentIndex + 1, params))
            {
                return result;
            }
        }

        for (const auto& childPtr : node->children | std::views::values)
        {
            if (RouteNode* child = childPtr.get(); child->isParam)
            {
                params[child->segment.substr(1)] = part;
                if (RouteNode* result = match(child, parts, currentIndex + 1, params))
                {
                    return result;
                }
                params.erase(child->segment.substr(1));
            }
            else if (child->isWild)
            {
                params[child->segment.substr(1)] = join(
                    std::vector(parts.begin() + static_cast<long>(index), parts.end()),
                    "/");
                return child;
            }
        }
        return nullptr;
    }
} // namespace cppkit::http
