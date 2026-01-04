#include "cppkit/http/server/http_server.hpp"
#include "cppkit/http/server/router_group.hpp"
#include "cppkit/json/json.hpp"

int main()
{
    using namespace cppkit::http::server;
    using namespace cppkit::json;

    // create server
    HttpServer server("127.0.0.1", 8888);

    // set static directory
    server.setStaticDir("static", "external");

    // register middleware
    server.addMiddleware("/",
                         [](const HttpRequest& req, HttpResponseWriter& res, const NextFunc& next)
                         {
                             std::cout << "Received request: " << httpMethodValue(req.getMethod()) << " " << req.
                                 getPath() << std::endl;
                             next();
                         });

    server.addMiddleware("/hello",
                         [](HttpRequest& req, HttpResponseWriter& res, const NextFunc& next)
                         {
                             auto name = req.getQuery("name");
                             if (name.empty())
                             {
                                 res.setStatusCode(cppkit::http::HTTP_BAD_REQUEST);
                                 res.setHeader("Content-Type", "text/plain");
                                 res.write("Missing 'name' query parameter");
                             }
                             else
                             {
                                 name += " from middleware";
                                 req.setQuery("name", name);
                                 next();
                             }
                         });


    // register routes
    // GET /hello
    server.Get("/hello",
               [](const HttpRequest& req, HttpResponseWriter& res)
               {
                   const auto name = req.getQuery("name");
                   res.setStatusCode(cppkit::http::HTTP_OK);
                   res.setContentType("text/plain");
                   res.write("Hello, World!" + name);
               });

    // GET /hello/:name
    server.Get("/hello/:name",
               [](const HttpRequest& req, HttpResponseWriter& res)
               {
                   // extract path parameter
                   const std::string name = req.getParam("name");
                   res.setStatusCode(cppkit::http::HTTP_OK);
                   res.setContentType("text/plain");
                   res.write("Hello, " + name + "!");
               });

    // POST /send
    server.Post("/send",
                [](const HttpRequest& req, HttpResponseWriter& res)
                {
                    // read body
                    auto body = req.readBody();

                    res.setStatusCode(cppkit::http::HTTP_OK);
                    res.setContentType("application/json");

                    auto result = Json{};
                    result["status"] = 200;

                    if (auto data = Json::parse(std::string(body.begin(), body.end())); data.isObject())
                    {
                        auto obj = data.asObject();
                        result["message"] = obj["name"].asString();
                    }
                    res.write(result.dump());
                });

    // create a route group for /user
    const auto group = server.group("/user");
    group.use(
        [](const HttpRequest& req, HttpResponseWriter& res, const NextFunc& next)
        {
            std::cout << "User group middleware for path: " << req.getPath() << std::endl;
            next();
        });
    group.Get("/:id",
              [](const HttpRequest& req, HttpResponseWriter& res)
              {
                  const std::string userId = req.getParam("id");
                  res.setStatusCode(cppkit::http::HTTP_OK);
                  res.setContentType("text/plain");
                  res.write("User ID: " + userId);
              });

    // start server
    server.start();
    return 0;
}
