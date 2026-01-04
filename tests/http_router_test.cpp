#include "cppkit/http/server/http_router.hpp"
#include <iostream>

void handleTest(const cppkit::http::server::HttpRequest& req, cppkit::http::server::HttpResponseWriter& rep)
{
    std::cout << "执行test" << std::endl;
}

int main()
{
    using namespace cppkit::http::server;

    Router router{};
    router.addRoute(cppkit::http::HttpMethod::Get, "/test/:id", handleTest);
    if (!router.exists(cppkit::http::HttpMethod::Get, "/test/123"))
    {
        throw std::runtime_error("路由不存在");
    }
    const auto handler = router.find(cppkit::http::HttpMethod::Get, "/test/123");
    const HttpRequest request(0);
    HttpResponseWriter writer(0);
    handler(request, writer);
    return 0;
}
